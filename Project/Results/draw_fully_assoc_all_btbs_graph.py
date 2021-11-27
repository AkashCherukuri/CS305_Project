import matplotlib.pyplot as plt
import numpy as np
import os


folders =  ['BaseLine',
			'FDIP',
			'Skewed']

folders_assoc =  ['BaseLine_Fully Assoc',
			'FDIP_Fully Assoc',
			'Skewed_Fully Assoc']

labels  =  ['BaseLine',
			'FDIP',
			'Skewed']

pred_idx = ['hashed_perceptron']

file_idx = ['client_001',
			'client_007',
			'server_001']

file_names = os.listdir('FDIP_Fully Assoc')

# Client/Server Num, predictor, BTB
data = np.zeros((3,2 ,3))

# Return the data from the file path
def get_IPC(path,i,j,k):
	content = open(path, "r").readlines()
	# IPC is in 33 for bimodal and gshare, 32 for hashed and perc; 5th "word"
	# print(path)
	# print(content[content.index("Region of Interest Statistics\n")+2])
	return float(content[content.index("Region of Interest Statistics\n")+2].split()[4])

def get_MPKI(path, i,j,k):
	content = open(path, "r").readlines()
	add = 0
	if labels[k] == 'Shotgun':
		add = 3
		return float(content[content.index("Region of Interest Statistics\n")+(-31+92)].split()[15+add])
	else:
		return float(content[content.index("Region of Interest Statistics\n")+(-31+102)].split()[15+add])


for folder in folders:
	for file in file_names:
		path = folder + "/" + file
		keys = file.split('-')

		i = file_idx.index(keys[0].split('.')[0])
		j = pred_idx.index(keys[1])
		k = folders.index(folder)

		#data[i,0,k] = get_MPKI(path,i,j,k)
		data[i,0,k] = get_IPC(path,i,j,k)

for folder in folders_assoc:
	for file in file_names:
		path = folder + "/" + file
		keys = file.split('-')

		i = file_idx.index(keys[0].split('.')[0])
		j = pred_idx.index(keys[1])
		k = folders_assoc.index(folder)

		#data[i,1,k] = get_MPKI(path,i,j,k)
		data[i,1,k] = get_IPC(path,i,j,k)


# Data has been filled, now make plots
colors = ['#cf6679', '#ffab40', '#4dd0e1', '#bb86fe']
# print(colors)
for plot_n in range(3):
	# set width of bar
	barWidth = 0.2
	fig, ax = plt.subplots(figsize =(15, 8))
	 	# Customize the plot
	ax.set_facecolor('#212121ff')
	fig.patch.set_facecolor('#212121ff')
	ax.spines['bottom'].set_color('#212121ff')
	ax.spines['top'].set_color('#212121ff')
	ax.spines['left'].set_color('#212121ff')
	ax.spines['right'].set_color('#212121ff')
	ax.xaxis.label.set_color('#ffffff')
	ax.yaxis.label.set_color('#ffffff')
	ax.title.set_color('#ffffff')
	ax.tick_params(color='#ffffff', labelcolor='#ffffff')
	ax.yaxis.grid(color='#353b41ff', linewidth=1.5)
	ax.set_axisbelow(True)
	# set height of bar
	# bim = list(data[plot_n, 0]/data[plot_n,0,0])
	# gsh = list(data[plot_n, 1]/data[plot_n,1,0])
	# hpr = list(data[plot_n, 2]/data[plot_n,2,0])
	# prc = list(data[plot_n, 3]/data[plot_n,3,0])
	
	base = list(data[plot_n, 0]/data[plot_n,0])
	fully_assoc = list(data[plot_n, 1]/data[plot_n,0])

	# base = list(data[plot_n, 0])
	# fully_assoc = list(data[plot_n, 1])

	# Set position of bar on X axis
	br1 = np.arange(len(base))
	br2 = [x + barWidth for x in br1]
	#br3 = [x + barWidth for x in br2]
	#br4 = [x + barWidth for x in br3]
	 
	# Make the plot
	plt.bar(br1, base, color =colors[1], width = barWidth,
	        edgecolor =colors[1], label ='Normal')
	plt.bar(br2, fully_assoc, color =colors[2], width = barWidth,
	        edgecolor =colors[2], label ='Fully Associative')
	#plt.bar(br3, hpr, color =colors[3] , width = barWidth,edgecolor =colors[3], label =pred_idx[2])
	#plt.bar(br4, prc, color =colors[4], width = barWidth,edgecolor =colors[4], label =pred_idx[3])
	 
	# Adding Xticks
	plt.xlabel('BTB', fontsize = 15, labelpad=15)
	plt.ylabel('IPC', fontsize = 15, labelpad=15)
	plt.ylim(0.98,1.03) # FOR IPC

	plt.xticks([r + barWidth/2 for r in range(len(base))],
	        labels)

	plt.title("Normalized IPC of fully associative for "+str(file_idx[plot_n]), fontsize=20)
	 
	plt.legend()
	plt.show()