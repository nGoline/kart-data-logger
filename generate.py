import csv

# Change this if you want to double the 'mock_lap.csv' instead
input_file = 'data/real_car_ride.csv' 
output_file = 'data/double_lap.csv'

try:
    with open(input_file, 'r') as f_in:
        reader = list(csv.reader(f_in))
        
    # Separate header (if exists) from data
    header = []
    data = reader
    try:
        float(reader[0][0]) # Test if the first row is an epoch number
    except ValueError:
        header = reader[0]
        data = reader[1:]

    if len(data) == 0:
        raise ValueError("CSV is empty!")

    # Find the very last epoch in the current file
    last_epoch = int(float(data[-1][0]))
    
    # Create the second lap by copying the data and incrementing the time
    second_lap = []
    current_epoch = last_epoch + 200 # Start 200ms after the last point

    for row in data:
        new_row = list(row)          # Copy the row
        new_row[0] = str(current_epoch) # Overwrite the epoch
        second_lap.append(new_row)
        current_epoch += 200         # Add 200ms for the next point

    # Combine the original lap and the duplicated lap
    combined_data = data + second_lap

    # Save to a new file
    with open(output_file, 'w', newline='') as f_out:
        writer = csv.writer(f_out)
        if header:
            writer.writerow(header)
        writer.writerows(combined_data)

    print(f"SUCCESS: Created {output_file}!")
    print(f"Original rows: {len(data)} | New total rows: {len(combined_data)}")
    print(f"Total simulated driving time: {(len(combined_data) * 200) / 1000} seconds.")

except FileNotFoundError:
    print(f"Error: Could not find '{input_file}'. Are you in the right folder?")