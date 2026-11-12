#include <assert.h>
#include <string.h>
#include <vector>
#include "drivers/storage/ums.hpp"

extern u8 ums_command(usb_mass_storage *, const u8 *, u8, void *, u32, bool, u32);

u64 timeradd = 0;
Timer::Timer() : Src(nullptr) {}
void usb_controller::scan_ports(TimerSource &) {}
usb_transfer_result usb_controller::usb_transfer(u32, void *, u32, TimerSource &, u32) {
    return {};
}

class FakeTimer : public TimerSource {
public:
    u64 read() override { return 0; }
    u64 read_freq() override { return 1000; }
};

struct Step {
    u32 handle;
    u32 length;
    std::vector<u8> response;
    int status;
    Step(u32 handle, u32 length, std::vector<u8> response,
         int status = TRANSFER_SUCCESS)
        : handle(handle), length(length), response(response), status(status) {}
};

class FakeController : public usb_controller {
protected:
    usb_transfer_result start_transfer(usb_async_request &, u32, void *, u32,
                                       TimerSource &) override { return {}; }
    void advance_transfer(usb_async_request &, TimerSource &) override {}
public:
    std::vector<Step> steps;
    std::vector<u8> sent;
    size_t next = 0;

    usb_transfer_result usb_transfer(u32 handle, void *buffer, u32 length,
                                     TimerSource &, u32) override {
        assert(next < steps.size());
        const Step &step = steps[next++];
        assert(handle == step.handle && length == step.length);
        usb_transfer_result result = {};
        result.status_code = step.status;
        if (step.status != TRANSFER_SUCCESS)
            return result;
        if (handle == 0) {
            assert(step.response.size() <= length);
            memcpy(buffer, step.response.data(), step.response.size());
            result.actual_length = step.response.size();
        } else {
            sent.insert(sent.end(), (u8 *)buffer, (u8 *)buffer + length);
            result.actual_length = length;
        }
        return result;
    }

    int controller_init(TimerSource &) override { return 0; }
    int enumrate_ports() override { return 0; }
    int reset_port(u8, TimerSource &) override { return 0; }
    int disable_port(u8, TimerSource &) override { return 0; }
    usb_device_descriptor identify_device(u8, TimerSource &) override { return {}; }
    usb_device set_address(usb_device_descriptor &, TimerSource &, u8) override { return {}; }
    void *usb_send_packet(usb_setup_packet, u32, u64, u8, TimerSource &, u8) override { return nullptr; }
    int configure_endpoints(usb_device, u16, u32, u8, TimerSource &) override { return 0; }
};

struct Fixture {
    FakeTimer source;
    Timer timer;
    FakeController controller;
    usb_endpoint endpoints[2] = {};
    usb_interface interface = {};
    usb_device device = {};
    usb_port_descriptor port = {};
    usb_mass_storage drive = {};

    explicit Fixture(u16 packet_size) {
        timer.Src = &source;
        timeradd = (u64)&timer;
        endpoints[0].address = 0x81;
        endpoints[1].address = 0x02;
        for (auto &endpoint : endpoints) {
            endpoint.transfer_type = 2;
            endpoint.max_packet_size = packet_size;
        }
        interface.endpoints = endpoints;
        interface.endpoint_count = 2;
        device.interfaces = &interface;
        device.usb_interface_count = 1;
        port.device = &device;
        controller.ports = &port;
        controller.ports_count = 1;
        drive.controller = &controller;
        drive.endpoint_in_addr = 0;
        drive.endpoint_out_addr = 1;
    }
};

static std::vector<u8> csw(u32 residue = 0) {
    std::vector<u8> bytes(13, 0);
    bytes[0] = 0x55; bytes[1] = 0x53; bytes[2] = 0x42; bytes[3] = 0x53;
    bytes[4] = 1; // First command tag.
    bytes[8] = residue;
    return bytes;
}

int main() {
    const u8 read_cdb[10] = {0x28};

    // A 4 KiB FAT cluster read must span multiple bulk packets under one CBW/CSW.
    {
        Fixture f(512);
        f.controller.steps.push_back({1, 31, {}});
        for (unsigned i = 0; i < 8; ++i)
            f.controller.steps.push_back({0, 512, std::vector<u8>(512, (u8)i)});
        f.controller.steps.push_back({0, 13, csw()});
        u8 data[4096] = {};
        assert(ums_command(&f.drive, read_cdb, 10, data, sizeof(data), true, 1000) == 0);
        assert(f.controller.next == f.controller.steps.size());
        assert(f.controller.sent.size() == 31);
        for (unsigned i = 0; i < sizeof(data); ++i)
            assert(data[i] == i / 512);
    }

    // Small packet endpoints also split the CBW and CSW.
    {
        Fixture f(8);
        auto status = csw();
        f.controller.steps = {{1, 8, {}}, {1, 8, {}}, {1, 8, {}}, {1, 7, {}},
                              {0, 8, std::vector<u8>(status.begin(), status.begin() + 8)},
                              {0, 5, std::vector<u8>(status.begin() + 8, status.end())}};
        const u8 ready_cdb[6] = {};
        assert(ums_command(&f.drive, ready_cdb, 6, nullptr, 0, false, 1000) == 0);
        assert(f.controller.sent.size() == 31);
        assert(f.controller.next == f.controller.steps.size());
    }

    // Writes use the OUT endpoint packet size for their data phase.
    {
        Fixture f(64);
        f.controller.steps = {{1, 31, {}}, {1, 64, {}}, {1, 64, {}}, {0, 13, csw()}};
        u8 data[128];
        memset(data, 0x5a, sizeof(data));
        assert(ums_command(&f.drive, read_cdb, 10, data, sizeof(data), false, 1000) == 0);
        assert(f.controller.sent.size() == 31 + sizeof(data));
        assert(memcmp(f.controller.sent.data() + 31, data, sizeof(data)) == 0);
    }

    // A short successful IN packet still cannot satisfy a full read.
    {
        Fixture f(64);
        f.controller.steps = {{1, 31, {}}, {0, 36, std::vector<u8>(10, 0x41)},
                              {0, 13, csw(26)}};
        u8 data[36] = {};
        assert(ums_command(&f.drive, read_cdb, 10, data, sizeof(data), true, 1000) == 1);
        assert(f.controller.next == f.controller.steps.size());
    }

    return 0;
}
