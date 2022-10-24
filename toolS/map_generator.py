"""
this script generates a mapping function in c++
"""

import matplotlib.pyplot as plt
import numpy as np
import time

def getTimestamp():
    return time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(time.time()))


### configuration --------------------------------------------------------------
# x range:
xMaxBits = 11  # 11 means: max x value is 2^11, so x-range from 0 to 2048 
stepsBits = 3  # 3 means 2^3 = 8 steps of x values that will be calculated as mainframes

# y range
y0 = 17300
yMax = 3200

# for better accuracy in int operations, we have to shift the values some bits to left... 
intIncreaseBitShift = 10

# function for the x/y graph
def f(x):
    if x < xMax / 2:
        x = x / xMax * np.pi
        return (yMid-yMax) * (np.sin(x + np.pi) +1) + yMid
    else:
        x = x / xMax * np.pi
        return (yMid-yMax) * (np.sin(x ) +1)  - 3850



### -----------------------------------------------------------------------------

xMax = np.power(2, xMaxBits)
steps = np.power(2, stepsBits)

xPerStep = xMax / steps
yMid = yMax + (y0 - yMax) / 2

intIncreaseFactor = np.power(2, intIncreaseBitShift)



# create an array with x values, and later the corresponding array with y

xArray = np.linspace(0, xMax, num=steps+1)
yList = []
diffList = []

yLast = 0

i = 0
for x in xArray:
    
    y = f(x)
    diff = (yLast -y ) / xPerStep * intIncreaseFactor
    y = int(y)
    yList.append(y)
    if i == 0:
        diff = 0 
    diffList.append(int(diff))    
    print ('%s %s %s' % (int(x), y, int(diff)))
    yLast = y
    i += 1    



#create the c++ code: 

def createArray(varName, valList):
    """
    int var[6] = { 1, 60,  0,  1,  0  };    
    """
    resultlist = []
    resultlist.append('// Definition for %s ... ' % varName)    
    sList = [str(x) for x in valList]
    resultlist.append('int %s[%s] = {%s};' % (varName, len(valList), ', '.join(sList)))    
    return resultlist


out = []
out.append('// <START OF GENERATED CODE> by map_generator.py --- %s -----' % getTimestamp())
out.append('// Interpolation for %s steps between x=0 and x=%s with %s x-values between each step'\
           % (steps, xMax, xPerStep))
out.append('// Delta values are shiftet by %s  bits left for better accuracacy. '\
           % (intIncreaseBitShift))
out.append('// ---------------------------------------------------------------------------\n')
out.extend(createArray('point_array', yList[:-1]))
out.extend(createArray('delta_array', diffList[1:]))
out.append('// ---------------------------------------------------------------------------\n')

nibbleShiftBits = xMaxBits - stepsBits

out.append("""
int xPartMask = (1 << %s) -1;
\n
int mapFunction(int x)
{
    byte segment = x >> %s;     //the number of the interpolation segment
    int x_part = x & xPartMask; // x value in the specified segment 
    int delta = ((long)delta_array[segment] * x_part)  >> %s;
    int y = point_array[segment] - delta;
    return y;
}

""" % (nibbleShiftBits, nibbleShiftBits, intIncreaseBitShift)
)


out.append('\n// <END OF GENERATED CODE> ----------------------------------------------//')

print ('\n'.join(out))

# plot the diagram... 
yArray = np.array(yList)
plt.plot(xArray, yArray)
plt.show()
