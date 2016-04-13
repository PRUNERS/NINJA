#!/usr/local/bin/python
import sys
if len(sys.argv) != 3:
    print "./nnm.py <input> <output>"
    exit(0)

import matplotlib
matplotlib.use('Agg')
import numpy as np
import matplotlib.pyplot as plt
import numbers

path = sys.argv[1]
output = sys.argv[2]
fd = open(path, "r")
x = []
y = []
warmup_net = 10
for line in fd:
    vals = line.split()
    if (len(vals) != 2): continue
    try:
        valx = float(vals[0]) / 1e6
#        if (valx < 4500): continue
#        valx = valx - 4500
        valy = float(vals[1])
    except :
        continue
    if (warmup_net > 0):
        warmup_net -= 1
        continue
    if isinstance(valx, float) and isinstance(valy, float):
        x.append(valx)
        y.append(valy)
#        print valx, valy

#plt.xlim([0, 800])
plt.xlim([0, 50])
plt.ylim([0, 12000])
plt.scatter(x, y)
#plt.show()
plt.savefig(output,  format="pdf")
