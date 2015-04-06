y1 = load('profile1.txt');
y2 = load('profile2.txt');

% y1 = load('../logs_ews_6apr/cs1_profile1.txt');
%%
% 
%   for x = 1:10
%       disp(x)
%   end
% 
% y2 = load('../logs_ews_6apr/cs1_profile2.txt');

y1_log = y1(:,2);
figure(1)
% loglog(y1_log, 'r')
semilogy(y1_log, 'r')
hold on
y2_log = y2(:,2);

% % loglog(y2_log, 'b')
semilogy(y2_log, 'b')


y1_log = y1(:,3);
figure(2)
% loglog(y1_log, 'r')
semilogy(y1_log, 'r')
hold on
y2_log = y2(:,3);

% % loglog(y2_log, 'b')
semilogy(y2_log, 'b')

% % unit = ones(1, 12000);


cs2_1 = load('cs2_1work.txt');
cs2_5 = load('cs2_5work.txt');
cs2_7 = load('cs2_7work.txt');
cpu_util_jiffies_1 = cs2_1(:, 4);
cpu_util_jiffies_5 = cs2_5(:, 4);
cpu_util_jiffies_7 = cs2_7(:, 4);


jiffies_1 = cs2_1(:, 1);
jiffies_5 = cs2_5(:, 1);
jiffies_7 = cs2_7(:, 1);


cpu_util_1 = cpu_util_jiffies_1 ./ jiffies_1;
cpu_util_5 = cpu_util_jiffies_5 ./ jiffies_5;
cpu_util_7 = cpu_util_jiffies_7 ./ jiffies_7;

cpu_util_1 = cpu_util_1 .* 100;
cpu_util_5 = cpu_util_5 .* 100;
cpu_util_7 = cpu_util_7 .* 100;

figure()
plot(cpu_util_1)

figure()
plot(cpu_util_5)
% 
figure()
plot(cpu_util_7)

sum_1 = 0;
sum_5 = 0;
sum_7 = 0;


% unit = ones(1, 12000);
% total_util_time_1 = unit * cpu_util_1
% total_util_time_5 = unit * cpu_util_5
% total_util_time_7 = unit * cpu_util_7
% 
% 
%  x(1) = total_util_time_1
%  x(2) = total_util_time_5
%  x(3) = total_util_time_7
%  
% %  histogram(x)
% % x = 1:length(y);
% 
% % x = x';
% 
% % figure(1)
% % %loglog(x,y1(:,2))
% % plot(x,y1(:,2))
% % 
% % % grid on
% % % hold on
% % figure(2)
% % plot(x,y2(:,2))
% 
% 
% 

