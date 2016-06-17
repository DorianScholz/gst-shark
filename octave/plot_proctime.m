#! /usr/bin/octave -qf

# Configuration
RESULT = 1;
FONTSIZE = 14;
LINEWIDTH = 1;

# Constants
TRUE = 1;
FALSE = 0;

[element_name_list, timestamp_mat, time_mat] = load_serie_timestamp_value('proctime.mat');

figure('Name','Processing time')
plot(timestamp_mat',time_mat','linewidth',LINEWIDTH)
title('Processing time','fontsize',FONTSIZE)
xlabel('time (seconds)','fontsize',FONTSIZE)
ylabel('time (nanoseconds)','fontsize',FONTSIZE)
legend(element_name_list)

print tracer -dpdf -append

