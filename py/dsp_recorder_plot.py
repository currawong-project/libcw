# Plot the output of a libcw:cwDspTransform.cpp: 'recorder' object.

import sys,os,json

import matplotlib.pyplot as plt

def plot_file( fname ):

    r = None
    with open(fname,"r") as f:
        r = json.load(f)

    idx = 0    
    while True:

        label = "{}".format(idx)

        if label not in r:
            break

        plt.plot(r[label])
        
        idx += 1

    plt.show()
            


if __name__ == "__main__":

    fname = os.path.expanduser("~/temp/temp_1.json")

    if len(sys.argv) > 1:
        fname = sys.argv[1]
    
    plot_file( fname )

    
