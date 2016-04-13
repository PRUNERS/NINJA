#!/usr/local/bin/python
#import matplotlib
#matplotlib.use('Agg')
import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt
import sys
path = sys.argv[1]
output = sys.argv[2]
type = sys.argv[3]
fd = open(path, "r")
traces = {}
for line in fd:
    if line.startswith("proc"):
        size = int(line.split()[1])
    vals = line.split()
    if len(vals) != 3: continue
    try:
        rank = int(vals[0])
        ts   = float(vals[1])
        src  = int(vals[2])
    except :
        continue
    if (not traces.has_key(rank)):
        traces[rank] = []
    traces[rank].append(ts)

data_summary = []
data = []
for rank in range(size):
    deltas = []
    if traces.has_key(rank):
        vec = traces[rank]
        for i in range(len(vec) - 1):
            deltas.append(vec[i+1] - vec[i])
            data_summary.append(vec[i+1] - vec[i])
    data.append(deltas)


fig = plt.figure()
ax = fig.add_subplot(111)

if type == "summary":
    bp = ax.boxplot(data_summary, 0, '')
    plt.xlabel("rank")
elif type == "all":
    bp = ax.boxplot(data, 0, '')
else:
    print "unknown type:", str(type)
    exit

plt.grid()
plt.ylabel("communication density")
plt.ylim([0, 0.001])

#plt.show()
plt.savefig(output + ".pdf", format="pdf")
