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



def fetch_result(random, bps, signtype,lane, velocity, spacing):
    res_RTB = []
    res_RDB = []
    res_fail_count = 0
    res_item_count = 0
#    dir_name = "lane"+str(lane)+"_velocity"+str(velocity)+"_spacing"+str(spacing)

    key = "random"+str(random)+"_bps"+str(bps)+"_signtype"+str(signtype)+"_lane"+str(lane)+"_velocity"+str(velocity)+"_spacing"+str(spacing)
    suffix = ".csv"
    fname = key+suffix

    list_dirs = os.walk(".") 
    for root, dirs, files in list_dirs: 
        for file_item in files:
            if file_item.find(fname) >= 0:
                RTB, RDB, fail_count, item_count= fetch_data(os.path.join(dir_name,file_item))
                res_RTB = res_RTB + RTB
                res_RDB = res_RDB + RDB
                res_fail_count = res_fail_count + fail_count
                res_item_count = res_item_count + item_count
    return list_average(res_RTB), list_average(res_RDB),res_fail_count, res_item_count


'''
fetch_data("/home/llfeng/VLC/simulator/RetroI2V_MAC/USE_ROUND/root/lane1_velocity2_spacing100/result-2019-01-26_23_35_39_lane1_velocity2_spacing100.csv")

'''
def gen_res_dict()
    para_tbl = {
        "random": [0,1],
        "bps": [128, 256],
        "signtype": [1,2],
        "lane": [1,2,3]
        "velocity": [30,50,70],
        "spacing": [100,500,1000]
    }

#    velocity = [30, 50, 70]
#    spacing = [100, 500, 1000]

#    random_tbl = [0, 1]
#    bps_tbl = [128, 256]
    res_dict = {}
    for random in para_tbl["random"]:
        for bps in para_tbl["bps"]:
            for signtype in para_tbl["signtype"]:
            	for lane in para_tbl["lane"]:   #lane: 1,2,3
            	    for j in range (0, len(para_tbl["velocity"])):  #velocity: 0,1,2
            	        for spacing in para_tbl["spacing"]:
            	            item_key = "random"+random+"_bps"+bps+"_signtype"+signtype+"_lane"+lane+"_velocity"+para_tbl["velocity"][j]+"_spacing"+spacing
            	            item_val = fetch_result(random,bps, signtype,lane,j,spacing)
            	            res_dict[item_key] = item_val   #[RTB, RDB, fail_count, res_item_count]

    data=DataFrame(res_dict)
    data.to_csv("parse_data.csv", index=False)
    print data
    return res_dict



def parse_plot_config(config_item_tbl, res_dict)
    para_tbl = {
        "random": [0,1],
        "bps": [128, 256],
        "signtype": [1,2],
        "lane": [1,2,3]
        "velocity": [30,50,70],
        "spacing": [100,500,1000]
    }

    plot_dict = {}
    config_count=0
    for config in config_item_tbl:
        for ele_val in para_tbl["spacing"]:
            key_name = "random" + config["random"] + "_bps" + config["bps"] + "_signtype" + config["signtype"] + "_lane" + config["lane"] + "_velocity" + config["velocity"] + "_spacing" + ele_val 
            plot_dict = plot_dict + res_dict[key_name];
    return plot_dict

def gen_plot_data()
    plot_config_tbl = {
        'work_range':[  
                        {"random":0, "bps":128, "signtype":1, "lane":2, "velocity":50, "spacing":-1}
                        {"random":0, "bps":128, "signtype":2, "lane":2, "velocity":50, "spacing":-1}
                        {"random":0, "bps":256, "signtype":1, "lane":2, "velocity":50, "spacing":-1}
                        {"random":0, "bps":256, "signtype":2, "lane":2, "velocity":50, "spacing":-1}
                    ],
        'road_traffic':[
                        {"random":0, "bps":128, "signtype":2, "lane":1, "velocity":30, "spacing":-1}, 
                        {"random":0, "bps":128, "signtype":2, "lane":2, "velocity":50, "spacing":-1}, 
                        {"random":0, "bps":128, "signtype":2, "lane":3, "velocity":70, "spacing":-1}
                    ],
        'tag_density':[
                        {"random":1, "bps":128, "signtype":2, "lane":1, "velocity":50, "spacing":-1}
                        {"random":1, "bps":128, "signtype":2, "lane":2, "velocity":50, "spacing":-1}
                        {"random":1, "bps":128, "signtype":2, "lane":3, "velocity":50, "spacing":-1}
                    ]
    }
    plot_res_dict = {}
    data_res_dict = gen_res_dict()
    for key, val in plot_config_tbl:
        plot_res_dict[key] = parse_plot_config(val, data_res_dict)

    print data_res_dict
    data = DataFrame(plot_res_dict)
    data.to_csv("plot_data.csv", index=False)


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
