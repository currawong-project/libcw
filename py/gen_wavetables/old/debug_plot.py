import csv

import matplotlib.pyplot as plt
import numpy as np


with open("/home/kevin/temp/temp_60_ch_0_c.csv") as f:
    rdr = csv.reader(f)

    smp_per_cycle = 183.468309

    idL = []
    yiL = []
    xiL = []
    fracL = []
    xV = []
    eV = []
    yV = []
    
    for r in rdr:
        idL.append(int(r[0]))
        iL.append(int(r[1]))
        yiL.append(int(r[2]))
        xiL.append(int(r[3]))
        fracL.append(float(r[4]))
        xV.append(float(r[5]))
        eV.append(float(r[6]))
        yV.append(float(r[7]))


    xf = [ x+f for x,f in zip(xiL,fracL) ]
        
    fig, ax = plt.subplots(4,1)

    ax[0].plot(xV)
    ax[1].plot(yV)
    ax[2].plot(eV)
    ax[3].plot(xf)
    
    plt.show()
        
        
