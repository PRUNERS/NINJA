#!/usr/local/bin/python
import sys, os
import subprocess
import threading



def distance_between_run_sets(dir_x, dir_y):
    xflist = os.listdir(dir_x)
    for i in range(len(xflist)):
        xflist[i] = dir_x + "/" + xflist[i]

    if dir_x == dir_y:
        xy_dist = []
#        distance_between_run_series(xflist, xflist, xy_dist)
        distance_between_run_series_parallel(xflist, xflist, xy_dist)
        print xy_dist
        return xy_dist, None

    yflist = os.listdir(dir_y)
    for i in range(len(yflist)):
        yflist[i] = dir_y + "/" + yflist[i]

    xy_dist = []
    yx_dist = []
    distance_between_run_series(xflist, yflist, xy_dist)
    distance_between_run_series(yflist, xflist, yx_dist)
    print xy_dist, yx_dist
    return xy_dist, yx_dist
    
def distance_between_run_series_parallel(files_x, files_y, xy_dist):
    num_thread = 32
    works = []
    results = []
    threads = []

    if num_thread > len(files_x):
        num_thread = len(files_x)

    for i in range(num_thread):
        works.append([])
        results.append([])

    index = 0
    for i in range(len(files_x)):
        works[index].append(files_x[i])
        index = (index + 1) % num_thread

    for i in range(num_thread):
        th = threading.Thread(target=distance_between_run_series, name=i, args=(works[i], works[i], results[i]))
        th.start()
        threads.append(th)

    for i in range(num_thread):
        threads[i].join()

    for i in range(num_thread):
        xy_dist += results[i]


def distance_between_run_series(files_x, files_y, min_xy):
    for file_x in files_x:
        min_xy_dis = -1
        for file_y in files_y:
            if file_x == file_y: continue
            xy_dis = distance_between_runs(file_x, file_y)
            if xy_dis < min_xy_dis or min_xy_dis == -1:
                min_xy_dis = xy_dis
        min_xy.append(min_xy_dis)
        print min_xy_dis
    return 

def distance_between_runs(file_x, file_y):
    cmd = "sdiff " + file_x + " " + file_y + " | grep '[<>]' | wc -l"
    add_del = subprocess.check_output(cmd, shell=True)
    cmd = "sdiff " + file_x + " " + file_y + " | grep '[|]'  | wc -l"
    replace = subprocess.check_output(cmd, shell=True)
    cmd = "wc -l " + file_x
    xwc = subprocess.check_output(cmd, shell=True)
    xwc = int(xwc.split()[0])
    cmd = "wc -l " + file_y
    ywc = subprocess.check_output(cmd, shell=True)
    ywc = int(ywc.split()[0])
    wc  = min(xwc, ywc)
    add_del = (int(add_del) - 1)/2.0 # -1 is for  <src> <tag> <comm_id> <id>
    replace = int(replace)
    return (add_del + replace) / wc


usage = """
 ./mor.py <dir1>
 ./mor.py <file1> <file2>
 ./mor.py <dir1> <dir2>
"""
def main():
    if len(sys.argv) == 1:
        print usage
        exit(0)
    elif len(sys.argv) == 2:
        x = sys.argv[1]
        if (os.path.isdir(x)):
            distance = distance_between_run_sets(x, x)
            print distance
        else:
            print "unknown file ", x, y
            print  usage
    elif len(sys.argv) == 3:
        x = sys.argv[1]
        y = sys.argv[2]
        if (os.path.isfile(x) and os.path.isfile(y)):
            distance = distance_between_runs(x, y)
            print distance
        elif (os.path.isdir(x) and os.path.isdir(y)):
            return distance_between_run_sets(x, y)
        else:
            print "unknown file ", x, y
            print usage
    else:
        print usage
        exit(0)


if __name__ == "__main__":
    main()
