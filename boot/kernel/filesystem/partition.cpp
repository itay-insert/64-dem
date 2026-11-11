#include "filesystem/partition.hpp"



void BootPartition::SetBootDrive(disk_drive *drive) {
    BootDrive = drive;
}