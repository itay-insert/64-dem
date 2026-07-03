user_input = input("Enter start_address in hex: ")
start_address = int(user_input, 0)
user_input = input("Enter next entry in hex: ")
next_entry = int(user_input, 0)
size = next_entry - start_address
choice = input("in which format do you want the size? GB, MB, KB, B or LBA?: ")
options = ["GB", "MB", "KB", "B", "LBA"]
division_factors = [1024**3, 1024**2, 1024, 1, 512]
outputs = ["Gigabytes", "Megabytes", "Kilobytes", "Bytes", "Sectors"]
for i in range(5):
    if choice == options[i]:
        result = size / division_factors[i]
        print(f"The size is: {result} {outputs[i]}")
        break
    