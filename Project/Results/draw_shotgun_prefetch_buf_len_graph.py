import matplotlib.pyplot as plt
import numpy as np
import os


folders =  ['Shotgun_Prefetch_buf_len_1',
			'Shotgun_Prefetch_buf_len_8',
			'Shotgun_Prefetch_buf_len_16',
            'Shotgun_Prefetch_buf_len_32',
            'Shotgun_Prefetch_buf_len_40',
            'Shotgun_Prefetch_buf_len_64']

labels  =  ['Shotgun_Prefetch_buf_len_1',
			'Shotgun_Prefetch_buf_len_8',
			'Shotgun_Prefetch_buf_len_16',
            'Shotgun_Prefetch_buf_len_32',
            'Shotgun_Prefetch_buf_len_40',
            'Shotgun_Prefetch_buf_len_64']

pred_idx = ['bimodal']

file_idx = ['client_001',
			'client_007',
			'server_001']

file_names = os.listdir('Shotgun_Prefetch_buf_len_8')

# Client/Server Num, predictor, BTB
data = np.zeros((3, 1, 6))

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
	#if labels[k] == 'Shotgun':
	add = 3
	return float(content[content.index("Region of Interest Statistics\n")+(-31+92)].split()[15+add])
	#else:
	    #return float(content[content.index("Region of Interest Statistics\n")+(-31+102)].split()[15+add])


for folder in folders:
	for file in file_names:
		path = folder + "/" + file
		keys = file.split('-')

		i = file_idx.index(keys[0].split('.')[0])
		j = pred_idx.index(keys[1])
		k = folders.index(folder)

		#data[i,j,k] = get_IPC(path,i,j,k)
		data[i,j,k] = get_MPKI(path,i,j,k)


# Data has been filled, now make plots
# plt.style.use('dark_background')
# colors = [v['color'] for v in plt.rcParams['axes.prop_cycle']]
# print(colors)
for plot_n in range(3):
	# set width of bar
	barWidth = 0.2
	fig, ax = plt.subplots(figsize =(15, 8))
	


	# set height of bar
	len_change = list(data[plot_n, 0]/data[plot_n,0,3])
	
	# bim = list(data[plot_n, 0])
	# gsh = list(data[plot_n, 1])
	# hpr = list(data[plot_n, 2])
	# prc = list(data[plot_n, 3])

	# Set position of bar on X axis
	br1 = np.array([1,8,16,32,40,64])
	logbr=np.log(br1)
	
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
	plt.bar(logbr, len_change, color ='#cf6679', width = barWidth,
	        edgecolor ='none', label =pred_idx[0])


	# Adding Xticks
	plt.xlabel('Prefetch_buffer_length', fontsize = 15)
	plt.ylabel('IPC',fontsize = 15)
	#plt.ylim(0.98,1.05) # FOR IPC

	plt.xticks(logbr,br1)
	plt.title("Normalized IPC for "+file_idx[plot_n], fontsize = 20)
	 
	plt.legend(labelcolor='white', facecolor='#414141ff', edgecolor='none')
	

	plt.show()