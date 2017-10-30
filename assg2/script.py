'''
The processCofig file is arranged as columns of Process id and corresponding Port no. Process Id 1 is always the time deamon.
'''

import os
import time

lines=0
with open ('processConfig','rb') as f:
    for line in f:
        lines+=1

#print count
x=1
#for x in range(1, 4):
while x <= lines:
	os.system ("gnome-terminal -e 'bash -c \"./causal.o " + str(x) + " | tee proc" + str(x) + "; exec bash\"'")
	#time.sleep(1)
	x += 1

#print("gnome-terminal -e 'bash -c \"echo Hello!; exec bash\"'")
#os.system("gnome-terminal -e 'bash -c \"echo Hello!; exec bash\"'")
#print ("gnome-terminal -e 'bash -c \"./berkley.o " + str(x) + " > proc" + str(x) + "; exec bash\"'")
#for x in range(1, 3):
	#print "./berkley.o " + str(x)
#	os.system("./berkley.o " + str(x) + " > proc" + str(x))
