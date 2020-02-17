CC=gcc
CFLAGS=-c -fPIC -O3 -std=c99
NAME=libmlv
OBJ_FOLDER=lib

main: MLVWriter.o MLVReader.o MLVFrameUtils.o MLVDataSource.o lj92.o
	ar rcs $(OBJ_FOLDER)/$(NAME).a $(OBJ_FOLDER)/*.o
	$(CC) -shared $(OBJ_FOLDER)/*.o -o $(OBJ_FOLDER)/$(NAME).so

MLVWriter.o: src/MLVWriter.c include/MLVWriter.h src/liblj92/lj92.h
	$(CC) $(CFLAGS) src/MLVWriter.c -o $(OBJ_FOLDER)/MLVWriter.o

MLVReader.o: src/MLVReader.c include/MLVReader.h src/liblj92/lj92.h
	$(CC) $(CFLAGS) src/MLVReader.c -o $(OBJ_FOLDER)/MLVReader.o

MLVFrameUtils.o: src/MLVFrameUtils.c include/MLVFrameUtils.h
	$(CC) $(CFLAGS) src/MLVFrameUtils.c -o $(OBJ_FOLDER)/MLVFrameUtils.o

MLVDataSource.o: src/MLVDataSource.c include/MLVDataSource.h
	$(CC) $(CFLAGS) src/MLVDataSource.c -o $(OBJ_FOLDER)/MLVDataSource.o

lj92.o: src/liblj92/lj92.c src/liblj92/lj92.h
	$(CC) $(CFLAGS) src/liblj92/lj92.c -o $(OBJ_FOLDER)/lj92.o