gcc -c -O3 libmlv_write.c
gcc -c -O3 libmlv_tools.c
gcc -c -O3 write_mlv.c

if [ ! -e libraw_r.a ]; then
	if [[ "$OSTYPE" == "linux-gnu" ]]; then
		echo "libraw_r.a is not present, add libraw_r.a to this folder"
		echo "Compile instructions: https://www.libraw.org/docs/Install-LibRaw.html"
		exit
	elif [[ "$OSTYPE" == "darwin"* ]]; then
		echo "libraw_r.a is not present"
		read -p "Download libraw_r.a? [y/n] " yn
		case $yn in
			[Yy]* )
				echo "Downloading libraw_r.a ..."
				wget -O ./libraw.zip https://www.libraw.org/data/LibRaw-0.19.5-MacOSX.zip &> /dev/null
				unzip libraw.zip &> /dev/null
				librawfolder=LibRaw-0.19.5
				cp ./$librawfolder/lib/libraw_r.a ./
				rm libraw.zip &> /dev/null
				rm -rf $librawfolder &> /dev/null
				echo "Downloaded"
				break;;
			[Nn]* ) echo "Put libraw_r.a in this folder thanks bye."; exit;;
			* ) exit;;
		esac
	else
		death "Unknown OS"
		exit;
	fi
else
	echo "libraw_r.a exists"
fi

# must add -lstdc++ only for libraw
gcc *.o libraw_r.a -o write_mlv -lstdc++
rm *.o