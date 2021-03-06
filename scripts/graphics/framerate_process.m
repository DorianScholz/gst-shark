# Configuration
RESULT = 0;

# Constants
TRUE = 1;
FALSE = 0;

# Open tracer data
fileID = fopen('framerate_fields.mat');
if -1 == fileID
    printf('Octave: ERROR: fopen cannot open the file %s\n',file_name);
    serie_name_list{1} = "";
    timestamp_mat = 0;
    value_mat = 0;
    return
end

# Compute how many fields has each event
serie_name_list_len = 1;
count = 1;
while (count == 1)
        [serie_name count] = fscanf(fileID,'%s',1);
        if (count == 0)
            break
        end
        # Load the field name in a list
        serie_name_list{serie_name_list_len} = serie_name;
        serie_name_list_len = serie_name_list_len + 1; 
end
serie_name_list_len = serie_name_list_len - 1;

fclose(fileID);

fileID = fopen('framerate_values.mat');

# Compute how many evens has the log
event_count = 0;
count = 1;
while (1 == count)
    [char_val, count] = fread(fileID,1,'char');
    if (char_val == '[')
        event_count = event_count + 1;
    end
end

if (0 == event_count)
    return
end

# Creata matrix to store the data
value_mat = nan(event_count,serie_name_list_len);
timestamp_mat = nan(event_count,serie_name_list_len);

frewind(fileID)

cpu_idx = 1;
count = 1;
for event_idx = 1 : event_count

    [timestamp count] = fscanf(fileID,'[%s]');
    if (count == 0)
        break
    end
    [timestamp_array] = sscanf(timestamp,'%d:%d:%f]');
    timestamp_val = timestamp_array(3) + (timestamp_array(2) * 60) + (timestamp_array(1) * 3600);
    timestamp_mat(event_idx,1:end) = timestamp_val;
    
    for serie_idx = 1 : (serie_name_list_len - 1)
        [val, count] = fscanf(fileID,'%f,',1);
        value_mat(event_idx,serie_idx) = val;
    end
    # Store the last field value value
    [val, count] = fscanf(fileID,'%f\n"');
    value_mat(event_idx,serie_name_list_len) = val;
    
end

fclose(fileID);

tracer.framerate.timestamp_mat = timestamp_mat;
tracer.framerate.fps_mat = value_mat;
tracer.framerate.element_name_list = serie_name_list;
