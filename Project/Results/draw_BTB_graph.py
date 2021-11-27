import matplotlib.pyplot as plt
import numpy as np
import os


folders =  ['BaseLine',
			'FDIP',
			'MBTB_Skewed_FOUR Variants', 
			'MBTB_Skewed_FOUR Variants_6K', 
			'MBTB_Skewed_FOUR Variants_8K', 
			'MBTB_Skewed_TWO Variants', 
			'MBTB_Skewed_TWO Variants_6K', 
			'MBTB_Skewed_TWO Variants_8K',
			'Shotgun',
			'Skewed']

labels  =  ['BaseLine',
			'FDIP',
			'MBTB_4', 
			'MBTB_4_6K', 
			'MBTB_4_8K', 
			'MBTB_2', 
			'MBTB_2_6K', 
			'MBTB_2_8K',
			'Shotgun',
			'Skewed']

pred_idx = ['bimodal',
		    'gshare',
		    'hashed_perceptron',
		    'perceptron']

file_idx = ['client_001',
			'client_007',
			'server_001']

file_names = os.listdir('FDIP')

# Client/Server Num, predictor, BTB
data = np.zeros((3, 4, 10))

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

		data[i,j,k] = get_IPC(path,i,j,k)
		#data[i,j,k] = get_MPKI(path,i,j,k)


# Data has been filled, now make plots
# plt.style.use('dark_background')
# colors = [v['color'] for v in plt.rcParams['axes.prop_cycle']]
# print(colors)
for plot_n in range(3):
	# set width of bar
	barWidth = 0.2
	fig, ax = plt.subplots(figsize =(15, 8))
	


	# set height of bar
	bim = list(data[plot_n, 0]/data[plot_n,0,0])
	gsh = list(data[plot_n, 1]/data[plot_n,1,0])
	hpr = list(data[plot_n, 2]/data[plot_n,2,0])
	prc = list(data[plot_n, 3]/data[plot_n,3,0])
	
	# bim = list(data[plot_n, 0])
	# gsh = list(data[plot_n, 1])
	# hpr = list(data[plot_n, 2])
	# prc = list(data[plot_n, 3])

	# Set position of bar on X axis
	br1 = np.arange(len(bim))
	br2 = [x + barWidth for x in br1]
	br3 = [x + barWidth for x in br2]
	br4 = [x + barWidth for x in br3]
	
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

	# Make the plot
	plt.bar(br1, bim, color ='#cf6679', width = barWidth,
	        edgecolor ='none', label =pred_idx[0])
	plt.bar(br2, gsh, color ='#ffab40', width = barWidth,
	        edgecolor ='none', label =pred_idx[1])
	plt.bar(br3, hpr, color ='#4dd0e1' , width = barWidth,
	        edgecolor ='none', label =pred_idx[2])
	plt.bar(br4, prc, color ='#bb86fe', width = barWidth,
	        edgecolor ='none', label =pred_idx[3])


	# Adding Xticks
	plt.xlabel('BTB', fontsize = 15)
	plt.ylabel('IPC',fontsize = 15)
	plt.ylim(0.98,1.05) # FOR IPC

	plt.xticks([r + barWidth for r in range(len(bim))],
	        labels)

	plt.title("Normalized IPC for "+file_idx[plot_n], fontsize = 20)
	 
	plt.legend(labelcolor='white', facecolor='#414141ff', edgecolor='none')
	

	plt.show()