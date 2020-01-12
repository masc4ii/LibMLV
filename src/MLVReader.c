/* 
 * MIT License
 *
 * Copyright (C) 2019 Ilia Sibiryakov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#include "../include/mlv_structs.h"

#define MLVReader_string "MLVReader 0.1"

#define MLVReader_header_block(BlockType, BlockName) \
struct \
{ \
    /* Store only one */ \
    union { \
        BlockType block; \
        mlv_hdr_t header; \
    }; \
    uint8_t exists; /* Was this block found in a file */ \
    uint8_t file_index; /* Which file (for split up MLVs - M00... M99) */ \
    uint16_t empty_space; \
    uint32_t num_blocks; /* How many of this block was found */ \
    uint64_t file_location; \
} BlockName;

/* This structure has some unused bytes */
typedef struct
{
    uint8_t block_type[4]; /* Block name string */
    uint8_t type; /* 0 = misc, 1 = block, 2 = audio frame, 3 = expo */
    uint8_t file_index; /* Which clip it is in (for split up files M00...M99) */
    uint64_t file_location; /* File location of block */
    uint64_t time_stamp;

    union
    {
        /* All blocks */
        struct {
            uint32_t offset; /* Offset from frame header to frame data */
            uint32_t size; /* Useful only audio/compressed video frames */
        } frame;

        /* EXPO blocks */
        struct {
            uint32_t iso; /* True ISO of data */
            uint32_t shutter; /* Exposure time in microseconds */
        } expo;
    };
} MLVReader_block_info_t;

typedef struct
{
    char string[16];

    /* Header blocks */
    MLVReader_header_block(mlv_file_hdr_t, MLVI)
    MLVReader_header_block(mlv_rawi_hdr_t, RAWI)
    MLVReader_header_block(mlv_wavi_hdr_t, WAVI)
    MLVReader_header_block(mlv_expo_hdr_t, EXPO)
    MLVReader_header_block(mlv_lens_hdr_t, LENS)
    MLVReader_header_block(mlv_rtci_hdr_t, RTCI)
    MLVReader_header_block(mlv_idnt_hdr_t, IDNT)
    MLVReader_header_block(mlv_info_hdr_t, INFO)
    MLVReader_header_block(mlv_diso_hdr_t, DISO)
    MLVReader_header_block(mlv_mark_hdr_t, MARK)
    MLVReader_header_block(mlv_styl_hdr_t, STYL)
    MLVReader_header_block(mlv_elvl_hdr_t, ELVL)
    MLVReader_header_block(mlv_wbal_hdr_t, WBAL)
    MLVReader_header_block(mlv_rawc_hdr_t, RAWC)

    /* Block counters */
    uint32_t num_blocks;
    uint32_t num_misc_blocks;
    uint32_t num_expo_blocks;
    uint32_t num_audio_frames;
    uint32_t num_video_frames;

    /* Some info */
    uint32_t biggest_video_frame; /* Biggest video frame size */

    /* Only used while parsing */
    uint8_t finished_parsing; /* Multiple files only go up to 100 */
    uint8_t file_index; /* Current file (Multiple files only go up to 100) */
    uint64_t file_pos; /* Position in current file */
    uint64_t total_frame_data; /* How many bytes of frame data have been seen */

    /* Array of block info, but sorted in to order by categories, then by
     * timestamp. Misc blocks first, then EXPOs, then AUDFs, then VIDFs */
    MLVReader_block_info_t blocks[/* num_blocks */];

} MLVReader_t;

#define MLVReader_src
#include "../include/MLVReader.h"
#undef MLVReader_src

/***** Wrapper for memory or a FILE, so they can be treated the same way ******/
typedef struct {
    int file_or_memory; /* 0 = file, 1 = memory */
    uint64_t size;
    union {
        uint8_t * mem;
        FILE * file;
    };
} mlvfile_t;
static void init_mlv_file_from_FILE(mlvfile_t * File, FILE * FileObject)
{
    File->file_or_memory = 0;
    File->file = FileObject;
    fseek(FileObject, 0, SEEK_END);
    File->size = ftell(FileObject);
}
static void init_mlv_file_from_mem(mlvfile_t * File, void * Mem, size_t Size)
{
    File->file_or_memory = 1;
    File->size = Size;
    File->mem = Mem;
}
static void uninit_mlv_file(mlvfile_t * File)
{
    return; /* Nothing to do (yet) */
}
static void mlv_file_get_data(mlvfile_t*F,uint64_t Pos,uint64_t Size,void*Out)
{
    if (F->file_or_memory == 1) memcpy(Out, F->mem + Pos, Size);
    else { fseek(F->file, Pos, SEEK_SET); fread(Out, 1, Size, F->file); }
}
static uint64_t mlv_file_get_size(mlvfile_t * File)
{
    return File->size;
}
/******************************************************************************/

static void print_size(uint64_t Size)
{
    if (Size < 1024)
        printf("%i Bytes\n", (int)Size);
    else if (Size < 1024*1024)
        printf("%.1f KiB\n", ((float)Size)/1024.0f);
    else if (Size < 1024*1024*1024)
        printf("%.2f MiB\n", ((float)Size)/(1024.0f*1024.0f));
    else
        printf("%.2f GiB\n", ((float)Size)/((float)(1024*1024*1024)));
}


/* Value for quicksort (so it gets sorted in correct order) */
static inline uint64_t BlockValue(MLVReader_block_info_t * Block)
{
    if (Block->type == 0)
    {/* TODO: Fix 262 terrabyte limit for correct sorting */
        /* For misc blocks, sort by file index, then file position */
        return ((((uint64_t)(Block->file_index)) << 48UL) || (Block->file_location & 0x0000FFFFFFFFFFFFUL));
    }
    else
    {
        /* For Frames/Audio/Exposure, sort by type, then time stamp */
        return ((((uint64_t)(Block->type)) << 56UL) || (Block->time_stamp & 0x0000FFFFFFFFFFFFUL));
    }
}

/* TODO: find better solution than copied recursive quicksort */
static void quicksort(MLVReader_block_info_t * Blocks, int first, int last)
{
   int i, j, pivot;

   MLVReader_block_info_t temp;

   if(first<last){
      pivot=first;
      i=first;
      j=last;

      while(i<j){
         while(BlockValue(&Blocks[i]) <= BlockValue(&Blocks[pivot]) && i<last)
            i++;
         while(BlockValue(&Blocks[j])>BlockValue(&Blocks[pivot]))
            j--;
         if(i<j){
            temp=Blocks[i];
            Blocks[i]=Blocks[j];
            Blocks[j]=temp;
         }
      }

      temp=Blocks[pivot];
      Blocks[pivot]=Blocks[j];
      Blocks[j]=temp;
      quicksort(Blocks,first,j-1);
      quicksort(Blocks,j+1,last);

   }
}

static void MLVReader_sort_blocks(MLVReader_t * Reader)
{
    quicksort(Reader->blocks, 0, Reader->num_blocks);
}

/* TODO: this function is pretty great, but could be made even cleaner */
static size_t init_mlv_reader( MLVReader_t * Reader, size_t ReaderSize,
                               mlvfile_t * File, int NumFiles,
                               int MaxFrames )
{
    /* TODO: FIX THIS */
    if (MaxFrames == 0) MaxFrames = INT32_MAX;

    /* Check if enough memory at all */
    if (ReaderSize < (sizeof(MLVReader_t) + sizeof(MLVReader_block_info_t)))
    {
        /* Most MLV clips out there are probably less than 800 blocks,
         * so this is a safe suggestion */
        return sizeof(MLVReader_t) + sizeof(MLVReader_block_info_t) * 800;
    }

    /* If not initialised, zero memory and put string at start */
    if (strcmp(MLVReader_string, (char *)Reader))
    {
        memset(Reader, 0, sizeof(MLVReader_t));
        strcpy(Reader->string, MLVReader_string);
    }

    /* How many blocks can fit in the memory given */
    uint32_t max_blocks = (ReaderSize-sizeof(MLVReader_t)) / sizeof(MLVReader_block_info_t);
    MLVReader_block_info_t * block = Reader->blocks; /* Output to here */

    /* File state */
    uint64_t current_file_size = mlv_file_get_size(File);

    /* While max blocks is not filled */
    while ( Reader->file_pos < current_file_size
         && Reader->num_blocks < max_blocks
         && Reader->file_index < NumFiles
         && Reader->num_video_frames < MaxFrames
         && !Reader->finished_parsing )
    {
        uint8_t block_name[4];
        uint32_t block_size;
        uint64_t time_stamp;
        mlv_file_get_data(File+Reader->file_index, Reader->file_pos, 4 * sizeof(uint8_t), block_name);
        mlv_file_get_data(File+Reader->file_index, Reader->file_pos+4, sizeof(uint32_t), &block_size);
        mlv_file_get_data(File+Reader->file_index, Reader->file_pos+8, sizeof(uint64_t), &time_stamp);

        /* Check if the file ends before block or if block is too small */
        if ((Reader->file_pos+block_size) > current_file_size || block_size < 16) {
            /* TODO: deal with this in some way */
        }

        /* Print block (unless its null or vidf) */
        if (strncmp((char *)block_name, "NULL", 4) && strncmp((char *)block_name, "VIDF", 4))
        {
            printf("%llu Block '%.4s' ", time_stamp, (char *)block_name);
            print_size(block_size);
        }

        /* Update block counts */
        if (strncmp((char *)block_name, "VIDF", 4) == 0) ++Reader->num_video_frames;
        if (strncmp((char *)block_name, "AUDF", 4) == 0) ++Reader->num_audio_frames;
        if (strncmp((char *)block_name, "EXPO", 4) == 0) ++Reader->num_expo_blocks;
        else ++Reader->num_misc_blocks;

        /* Set info for other default blocks... */
        #define MLVReader_read_block(BlockType) \
        { \
            if (memcmp((char *)block_name, #BlockType, 4) == 0) \
            { \
                /* Store first instance of each block */ \
                if (Reader->BlockType.num_blocks == 0) \
                { \
                    mlv_file_get_data( File+Reader->file_index, \
                                       Reader->file_pos, \
                                       sizeof(Reader->BlockType.block), \
                                       &Reader->BlockType.block ); \
                } \
                Reader->BlockType.num_blocks++; \
            } \
        }

        MLVReader_read_block(MLVI)
        MLVReader_read_block(RAWI)
        MLVReader_read_block(WAVI)
        MLVReader_read_block(EXPO)
        MLVReader_read_block(LENS)
        MLVReader_read_block(RTCI)
        MLVReader_read_block(IDNT)
        MLVReader_read_block(INFO)
        MLVReader_read_block(DISO)
        MLVReader_read_block(MARK)
        MLVReader_read_block(STYL)
        MLVReader_read_block(ELVL)
        MLVReader_read_block(WBAL)
        MLVReader_read_block(RAWC)

        /* Set block info */
        {
            memcpy(block->block_type, block_name, 4);
            block->file_index = Reader->file_index;
            block->file_location = Reader->file_pos;
            block->time_stamp = time_stamp;

            if (strncmp((char *)block_name, "AUDF", 4) == 0)
            {
                mlv_file_get_data( File+Reader->file_index,
                                   Reader->file_pos+offsetof(mlv_audf_hdr_t,frameSpace),
                                   sizeof(uint32_t), &block->frame.offset );
                block->frame.size = block_size - (sizeof(mlv_audf_hdr_t) + block->frame.offset);
                Reader->total_frame_data += block_size;
            }
            else if (strncmp((char *)block_name, "VIDF", 4) == 0)
            {
                mlv_file_get_data( File+Reader->file_index,
                                   Reader->file_pos+offsetof(mlv_vidf_hdr_t,frameSpace),
                                   sizeof(uint32_t), &block->frame.offset );
                block->frame.size = block_size - (sizeof(mlv_vidf_hdr_t) + block->frame.offset);
                Reader->total_frame_data += block_size;
            }
            else if (strncmp((char *)block_name, "EXPO", 4) == 0)
            {
                mlv_file_get_data( File+Reader->file_index,
                                   Reader->file_pos+offsetof(mlv_expo_hdr_t,isoAnalog),
                                   sizeof(uint32_t), &block->expo.iso );
                uint64_t expo_microseconds;
                mlv_file_get_data( File+Reader->file_index,
                                   Reader->file_pos+offsetof(mlv_expo_hdr_t,isoAnalog),
                                   sizeof(uint32_t), &expo_microseconds );
                block->expo.shutter = (uint32_t)expo_microseconds;
            }
            else {;} /* No special info stored for other types */
        }

        Reader->file_pos += block_size;
        ++Reader->num_blocks;
        ++block;

        /* Go to next file if reached end, unless its last file anyway */
        if (Reader->file_pos >= current_file_size && Reader->file_index != (NumFiles-1))
        {
            ++Reader->file_index;
            Reader->file_pos = 0;
            current_file_size = mlv_file_get_size(File+Reader->file_index);
            printf("video frames: %i\n\n", Reader->num_video_frames);
        }
    }

    /* If not scanned through all files, we need more memory */
    if (!Reader->finished_parsing && (Reader->file_pos < current_file_size || Reader->file_index != (NumFiles-1)))
    {
        /* Estimate memory required for whole MLV to be read */
        uint64_t total_file_size = 0;
        for (int f = 0; f < NumFiles; ++f)
            total_file_size += mlv_file_get_size(File+Reader->file_index);

        uint64_t average_frame_size = 100;
        if (Reader->total_frame_data != 0)
        {
            average_frame_size = Reader->total_frame_data / (Reader->num_audio_frames + Reader->num_video_frames);
        }
        else
        {
            /* Lets say it's an EOS M shooting 10 bit lossless (kinda the lowest common denominator) */
            average_frame_size = ((1728 * 978 * 10) / 8) * 0.57;
        }

        /* An estimate: 10 misc blocks per file */
        return ReaderSize + (average_frame_size*total_file_size + 20 + 10*NumFiles) * sizeof(MLVReader_block_info_t);
    }
    else
    {
        /* Sort blocks, then return memory used, done */
        MLVReader_sort_blocks(Reader);
        puts("great success!");
        Reader->finished_parsing = 1;
        return sizeof(MLVReader_t) + sizeof(MLVReader_block_info_t) * Reader->num_blocks;
    }
}

/* Initialise MLV reader from FILEs */
int64_t init_MLVReaderFromFILEs( MLVReader_t * Reader,
                                 size_t ReaderSize,
                                 FILE ** Files,
                                 int NumFiles,
                                 int MaxFrames )
{
    if (NumFiles > 101) return MLVReader_ERROR_BAD_INPUT;
    if (NumFiles <= 0) return MLVReader_ERROR_BAD_INPUT;
    if (Reader == NULL) return MLVReader_ERROR_BAD_INPUT;
    if (Files == NULL) return MLVReader_ERROR_BAD_INPUT;
    for (int f = 0; f < NumFiles; ++f) {
        if (Files[f] == NULL) return MLVReader_ERROR_BAD_INPUT;
    }

    mlvfile_t mlv_files[101];

    for (int f = 0; f < NumFiles; ++f) {
        init_mlv_file_from_FILE(mlv_files+f, Files[f]);
    }

    int64_t return_value = init_mlv_reader( Reader, ReaderSize,
                                            mlv_files, NumFiles, MaxFrames );

    for (int f = 0; f < NumFiles; ++f) {
        uninit_mlv_file(mlv_files+f);
    }

    return return_value;
}

/* Initialise MLV reader from memory */
int64_t init_MLVReaderFromMemory( MLVReader_t * Reader,
                                  size_t ReaderSize,
                                  void ** Files,
                                  uint64_t * FileSizes,
                                  int NumFiles,
                                  int MaxFrames )
{
    if (NumFiles > 101) return MLVReader_ERROR_BAD_INPUT;
    if (NumFiles <= 0) return MLVReader_ERROR_BAD_INPUT;
    if (Reader == NULL) return MLVReader_ERROR_BAD_INPUT;
    if (Files == NULL) return MLVReader_ERROR_BAD_INPUT;
    if (FileSizes == NULL) return MLVReader_ERROR_BAD_INPUT;
    for (int f = 0; f < NumFiles; ++f) {
        /* 10TB seems like a reasonable file size limit */
        if (FileSizes[f] > (1024UL*1024*1024*10)) return MLVReader_ERROR_BAD_INPUT;
        if (Files[f] == NULL) return MLVReader_ERROR_BAD_INPUT;
    }

    /* Max is 101 file chunks right? 1x MLV + up to 100x M00-M99 */
    mlvfile_t mlv_files[101];

    for (int f = 0; f < NumFiles; ++f) {
        init_mlv_file_from_mem(mlv_files+f, Files[f], FileSizes[f]);
    }

    size_t return_value = init_mlv_reader( Reader, ReaderSize,
                                           mlv_files, NumFiles, MaxFrames );

    for (int f = 0; f < NumFiles; ++f) {
        uninit_mlv_file(mlv_files+f);
    }

    return return_value;
}

void uninit_MLVReader(MLVReader_t * Reader)
{
    /* Nothing to do as usual */
    return;
}

int32_t MLVReaderGetNumBlocks(MLVReader_t * Reader)
{
    return Reader->num_blocks;
}

int32_t MLVReaderGetNumBlocksOfType(MLVReader_t * Reader, char * BlockType)
{
    if (BlockType == NULL) return Reader->num_blocks;

    if (memcmp(BlockType, "VIDF", 4) == 0) return Reader->num_video_frames;
    if (memcmp(BlockType, "AUDF", 4) == 0) return Reader->num_audio_frames;

    #define MLVReader_block_count_check(CheckBlockType) \
        if (strcmp(BlockType, #CheckBlockType) == 0) return Reader->CheckBlockType.num_blocks;

    MLVReader_block_count_check(MLVI)
    MLVReader_block_count_check(RAWI)
    MLVReader_block_count_check(WAVI)
    MLVReader_block_count_check(EXPO)
    MLVReader_block_count_check(LENS)
    MLVReader_block_count_check(RTCI)
    MLVReader_block_count_check(IDNT)
    MLVReader_block_count_check(INFO)
    MLVReader_block_count_check(DISO)
    MLVReader_block_count_check(MARK)
    MLVReader_block_count_check(STYL)
    MLVReader_block_count_check(ELVL)
    MLVReader_block_count_check(WBAL)
    MLVReader_block_count_check(RAWC)

    /* Now if none of the previous checks returned, actually count blocks */
    uint32_t blockstr = *((uint32_t *)((void *)BlockType)); /* For faster comparison */
    uint32_t block_count = 0;
    uint32_t num_misc_blocks = Reader->num_blocks - Reader->num_video_frames - Reader->num_audio_frames - Reader->num_expo_blocks;

    for (int b = 0; b < num_misc_blocks; ++b)
    {
        uint32_t current_blockstr = *((uint32_t *)((void *)&Reader->blocks[b]));
        if (current_blockstr == blockstr) ++block_count;
    }
}

/* Get block data for block of BlockType, BlockIndex = 0 to get first
 * instance of that block, 1 to get second, etc. Bytes argument is maximum
 * number of bytes to get. Outputs to Out. */
int64_t MLVReaderGetBlockDataFromFiles( MLVReader_t * Reader, FILE ** Files,
                                        char * BlockType, int BlockIndex,
                                        size_t Bytes, void * Out );
int64_t MLVReaderGetBlockDataFromMemory( MLVReader_t * Reader, void ** Files,
                                         char * BlockType, int BlockIndex, 
                                         size_t Bytes, void * Out );

/* Returns memory needed for using next two functions (void * DecodingMemory) */
size_t MLVReaderGetFrameDecodingMemorySize(MLVReader_t * MLVReader);

/* Gets an undebayered frame from MLV file */
void MLVReaderGetFrameFromFile( MLVReader_t * MLVReader,
                                FILE ** Files,
                                void * DecodingMemory,
                                uint64_t FrameIndex,
                                uint16_t * FrameOutput );

/* Gets undebayered frame from MLV in memory */
void MLVReaderGetFrameFromMemory( MLVReader_t * MLVReader,
                                  void ** Files,
                                  void * DecodingMemory,
                                  uint64_t FrameIndex,
                                  uint16_t * FrameOutput );

/****************************** Metadata getters ******************************/

int MLVReaderGetFrameWidth(MLVReader_t * Reader)
{
    return Reader->RAWI.block.xRes;
}

int MLVReaderGetFrameHeight(MLVReader_t * Reader)
{
    return Reader->RAWI.block.yRes;
}

int MLVReaderGetBlackLevel(MLVReader_t * Reader)
{
    return Reader->RAWI.block.raw_info.black_level;
}

int MLVReaderGetWhiteLevel(MLVReader_t * Reader)
{
    return Reader->RAWI.block.raw_info.white_level;
}

int MLVReaderGetBitdepth(MLVReader_t * Reader)
{
    return Reader->RAWI.block.raw_info.bits_per_pixel;
}

// /* Which pixel bayer pattern starts at in the top left corner (RGGB, 0-3) */
// int MLVReaderGetBayerPixel(MLVReader_t * Reader)
// {
//     return 0;
// }

/* Returns two ints to Out */
int MLVReaderGetPixelAspectRatio(MLVReader_t * Reader, int * Out)
{
    /* TODO: figure out the rawc block */
    // if (Reader->RAWC.num_blocks != 0)
    // {
    //     /* code */
    // }
    // else
    {
        /* TODO: detect aspect on old 3x5 clips before RAWC */
        Out[0] = 1;
        Out[1] = 1;
    }
}

/* FPS value */
double MLVReaderGetFPS(MLVReader_t * Reader);

/* Get top and bottom of the FPS fraction */
int32_t MLVReaderGetFPSNumerator(MLVReader_t * Reader)
{
    return Reader->MLVI.block.sourceFpsNom;
}

int32_t MLVReaderGetFPSDenominator(MLVReader_t * Reader)
{
    return Reader->MLVI.block.sourceFpsDenom;
}

void MLVReaderGetCameraName(MLVReader_t * Reader, char * Out)
{
    strcpy(Out, (char *)Reader->IDNT.block.cameraName);
}

void MLVReaderGetLensName(MLVReader_t * Reader, char * Out)
{
    if (Reader->LENS.block.lensName[0] != 0)
        strcpy(Out, (char *)Reader->LENS.block.lensName);
    else
        strcpy(Out, "No electronic lens");
}

int MLVReaderGetLensFocalLength(MLVReader_t * Reader)
{
    return Reader->LENS.block.focalLength;
}

int MLVReaderGetISO(MLVReader_t * Reader, uint64_t FrameIndex)
{
    if (Reader->num_expo_blocks == 0) return MLVReader_ERROR_METADATA_NOT_AVAILABLE;
    if (FrameIndex >= Reader->num_video_frames) return MLVReader_ERROR_BAD_INPUT;

    MLVReader_block_info_t * frame = Reader->blocks + (Reader->num_blocks-Reader->num_video_frames+FrameIndex);
    uint64_t frame_timestamp = frame->time_stamp;

    MLVReader_block_info_t * expo_blocks = Reader->blocks + Reader->num_misc_blocks;

    for (int b = 0; b < Reader->num_expo_blocks-1; ++b)
    {
        if ( expo_blocks[ b ].time_stamp < frame_timestamp
          && expo_blocks[b+1].time_stamp > frame_timestamp )
        {
            return expo_blocks[b].expo.iso;
        }
    }

    return expo_blocks[Reader->num_expo_blocks-1].expo.iso;
}