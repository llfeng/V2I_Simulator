# -*- coding: utf-8 -*- 
import os 
import numpy as np
from pandas.core.frame import DataFrame
import math
import fileinput


g_com_dist_up = 65.0
g_sys_fov = 20.0/2


def fetch_data(filename):
    RTB = []
    RDB = []
    fail_count = 0
    item_count = 0
    for line in fileinput.input(filename):
        item_count = item_count + 1
        pos_item = line.split(',')
        if float(pos_item[3]) > float(pos_item[1]):
#            com_x_left = math.sqrt(pow(g_com_dist_up, 2) - pow(float(pos_item[2]),2));
#            com_x_right = float(pos_item[2]) / math.tan(g_sys_fov);
#            com_dist = com_x_left-com_x_right
#            remain_dist = float(pos_item[3]) - float(pos_item[1]) - com_x_right
            com_dist = float(pos_item[5])
            remain_dist = float(pos_item[6])

            RTB.append((com_dist-remain_dist)/com_dist)
            RDB.append(remain_dist/com_dist)
        else:
#            print(pos_item[3], pos_item[1])
            fail_count = fail_count + 1
    return RTB, RDB, fail_count, item_count

def list_average(num):
    nsum = 0
    for i in range(len(num)):
        nsum = nsum + num[i]
    return nsum / len(num)



def fetch_result(lane, velocity, spacing):
    res_RTB = []
    res_RDB = []
    res_fail_count = 0
    res_item_count = 0
    dir_name = "lane"+str(lane)+"_velocity"+str(velocity)+"_spacing"+str(spacing)
    fname = dir_name+".csv"
    list_dirs = os.walk(".") 
    for root, dirs, files in list_dirs: 
        for file_item in files:
            if file_item.find(fname) >= 0:
#                print file_item
                RTB, RDB, fail_count, item_count= fetch_data(dir_name+"/"+file_item)
                res_RTB = res_RTB + RTB
                res_RDB = res_RDB + RDB
                res_fail_count = res_fail_count + fail_count
                res_item_count = res_item_count + item_count
    return list_average(res_RTB), list_average(res_RDB),res_fail_count, res_item_count

velocity = [30, 50, 70]
spacing = [100, 500, 1000]

'''
fetch_data("/home/llfeng/VLC/simulator/RetroI2V_MAC/USE_ROUND/root/lane1_velocity2_spacing100/result-2019-01-26_23_35_39_lane1_velocity2_spacing100.csv")

'''
ave_RDB = 0.0
fail_count = 0
total_count = 0
res_dict = {}
for i in range(1, 4):   #lane: 1,2,3
    for j in range (0, 3):  #velocity: 0,1,2
        for k in spacing:
#            print(i,j,k)
#            ave_RDB, fail_count, total_count = fetch_result(i,j,k)
            item_key = "lane"+str(i)+"_velocity"+str(velocity[j])+"_spacing"+str(k)
            item_val = fetch_result(i,j,k)
            res_dict[item_key] = item_val
data=DataFrame(res_dict)
data.to_csv("data.csv", index=False)
print data
#print(res_dict)



'''
def do_cal(rootDir, write_file): 
    write_dict={}
#    wf_fd = open(write_file, "rb+")
    list_dirs = os.walk(rootDir) 
    for root, dirs, files in list_dirs: 
#        for d in dirs: 
#            print d
        for f in files: 
            file_name = f.find('result-')
            if file_name >= 0:
                strlist = f.split('_')
                reader_str = strlist[4]
                tag_str_list = strlist[5].split('.')
                tag_str = tag_str_list[0]
                dict_name=reader_str[0:1]+'-'+tag_str[0:1]

		res_fd=open(dict_name + '/' + f,"rb")
#                np.array(value_tab)
                value_tab=[]
                i=0
		while 1:
                    line = res_fd.readline()
                    if not line:
                        break
                    number_str_list = line.split(':')
                    write_str = number_str_list[1];
                    value_tab.append(int(write_str))
                    i=i+1
		res_fd.close()
                if i==100:
                    write_dict[reader_str+'_'+tag_str]=value_tab
#                print write_dict
#                print reader_str+'_'+tag_str
    data=DataFrame(write_dict)
    data.to_csv("data.csv", index=False)
    print data
#    wf_fd.close()

write_file_path="aaa.csv"
do_cal(".", write_file_path)
'''
