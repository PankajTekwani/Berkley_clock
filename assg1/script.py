import os

for x in range(1, 3):
	#print "./berkley.o " + str(x)
	os.system("./berkley.o " + str(x) + " > proc" + str(x))
