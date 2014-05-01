/*
 *  File name: gif.c
 *  Created on: 15. 3. 2014
 *  Author: Adam Siroky
 *  Login: xsirok07
 *  Type: Source file
 *  Description: Include functions for gif decompression
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include "gif2bmp.h"
#include "gif.h"
#include "bmp.h"
#include "dictionary.h"
#include "constant.h"

/**
 * Function increment bmp output buffer pointer
 *
 * @param bmpWriter BMP writer structure - include logical screen size, position and offset
 */
void incBMPBufferPointer(tBMPWRITER *bmpWriter) {

	bmpWriter->actualColumn++;
	if (bmpWriter->actualColumn == (bmpWriter->actualX + bmpWriter->actualWidth)) {

		bmpWriter->actualColumn = bmpWriter->actualX;
		bmpWriter->actualRow++;
		if (bmpWriter->actualRow == (bmpWriter->actualY + bmpWriter->actualHeight)) {
			bmpWriter->actualRow = bmpWriter->actualY;
		}
	}
}

/**
 * Function save color structure to BMP output buffer on writer position
 *
 * @param color Index of color to color table
 * @param bmpOutputBuffer Pointer to bmp output buffer
 * @param colorTable Pointer to color table
 * @param bmpWriter	Pointer to BMP writer structure - include logical screen size, position and offset
 */
void processColor(int color, tBGR **bmpOutputBuffer, tRGB *colorTable, tBMPWRITER *bmpWriter) {

	// Save color to BMP output buffer
	bmpOutputBuffer[bmpWriter->actualRow][bmpWriter->actualColumn].blue = colorTable[color].blue;
	bmpOutputBuffer[bmpWriter->actualRow][bmpWriter->actualColumn].red = colorTable[color].red;
	bmpOutputBuffer[bmpWriter->actualRow][bmpWriter->actualColumn].green = colorTable[color].green;

	// Increment BMP output buffer pointer
	incBMPBufferPointer(bmpWriter);
}

/**
 * Function save dictionary item/color list into BMP output buffer
 *
 * @param list List of colors - one item of dictionary
 * @param bmpOutputBuffer Pointer to bmp output buffer
 * @param colorTable Pointer to color table
 * @param bmpWriter Pointer to BMP writer structure - include logical screen size, position and offset
 */
void processColorList(tDIC_ITEM *list, tBGR **bmpOutputBuffer, tRGB *colorTable, tBMPWRITER *bmpWriter) {

	// Get first color of the list
	tLIST_ITEM *item = list->first;

	// Process whole color list
	while (item != NULL) {

		// Process item color
		processColor(item->colorTableIndex, bmpOutputBuffer, colorTable, bmpWriter);

		// Get next color
		item = item->nextColor;
	}
}

/**
 * Function process image data block and save color for each pixel into buffer
 *
 * @param inputFile Pointer to input GIF file
 * @param outputFile Pointer to output BMP file
 * @param reader Pointer to GIF reader structure
 * @param bmpOutputBuffer Pointer to BMP output buffer
 * @param bmpWriter Pointer to BMP writer structure - include logical screen size, position and offset
 * @return 0 on success, 1 on failure
 */
int getImageData(FILE *inputFile, FILE *outputFile, tGIFREADER *reader, tBGR **bmpOutputBuffer, tBMPWRITER *bmpWriter) {

	u_int8_t Byte = 0;
	int readRetVal = 0;
	u_int32_t readedBits;
	u_int32_t processedPixels = 0;
	tDICTIONARY dictionary;
	int K = 0;				 // First color index to color table of color list
	int firstCodeAfterCC = 0;// Control values
	int subBlockStatus = UNFINISHED_SUBBLOCK;

	// Get LZW size
	readRetVal = readByteFromFile(inputFile, &Byte);
	if (readRetVal == READ_WRITE_ERR) {
		fprintf(stderr, "%s", "Can not read input file.");
		return EXIT_FAILURE;
	}
	else if (readRetVal == END_OF_FILE) {
		fprintf(stderr, "%s", "Incorrect gif file.");
		return EXIT_FAILURE;
	}
	else {
		reader->lzwSize = (int)Byte;
		reader->initLzwSize = reader->lzwSize;
	}

	// Get block size
	readRetVal = readByteFromFile(inputFile, &Byte);
	if (readRetVal == READ_WRITE_ERR) {
		fprintf(stderr, "%s", "Can not read input file.");
		return EXIT_FAILURE;
	}
	else if (readRetVal == END_OF_FILE) {
		fprintf(stderr, "%s", "Incorrect gif file.");
		return EXIT_FAILURE;
	}
	else
		reader->BytesToReadInSubBlock = Byte;

	// Init dictionary
	if(initDictionary (&dictionary, reader))
		return EXIT_FAILURE;

	// Read first code - should be CC
	if (readBitsStreamFromFile (inputFile, reader, &readedBits, subBlockStatus))
		return EXIT_FAILURE;

	// CC code
	if (readedBits == dictionary.clearCode) {}
	// Wrong code
	else {
		fprintf(stderr, "%s", "Incorrect gif file.");
		return EXIT_FAILURE;
	}

	// Read first data code
	if (readBitsStreamFromFile (inputFile, reader, &readedBits, subBlockStatus))
		return EXIT_FAILURE;

	// Check the existence of the code in the dictionary
	if (codeInDictionary(&dictionary, &readedBits)) {
		fprintf(stderr, "%s", "Incorrect gif file.");
		return EXIT_FAILURE;
	}
	else {

		// Process pixel
		processColor(readedBits, bmpOutputBuffer, reader->activeColorTable, bmpWriter);

		// Pixel processed
		processedPixels++;

		// Set CODE-1
		dictionary.previousCode = readedBits;
	}

	// Read whole data block
	while (processedPixels <= reader->dataBlockSize) {

		// Get block size
		if (reader->BytesToReadInSubBlock == 0) {
			readRetVal = readByteFromFile(inputFile, &Byte);
			if (readRetVal == READ_WRITE_ERR) {
				fprintf(stderr, "%s", "Can not read input file.");
				freeDictionary(&dictionary);
				return EXIT_FAILURE;
			}
			else if (readRetVal == END_OF_FILE) {
				fprintf(stderr, "%s", "Incorrect gif file.");
				freeDictionary(&dictionary);
				return EXIT_FAILURE;
			}
			else
				reader->BytesToReadInSubBlock = Byte;

			// Start new subblock
			subBlockStatus = NEW_SUBBLOCK;
		}

		// Zero sub block size - end of data block
		if (reader->BytesToReadInSubBlock == 0) {

			// Get missing pixels from bytes buffer in reader
			while (processedPixels <= reader->dataBlockSize) {

				// Dont read next byte from stream, just process buffered bytes
				reader->BytesToReadInSubBlock = 0;

				// Read code form block
				if (readBitsStreamFromFile (inputFile, reader, &readedBits, subBlockStatus)) {
					freeDictionary(&dictionary);
					return EXIT_FAILURE;
				}

				// Last code must be end of information
				if (processedPixels == reader->dataBlockSize) {
					if (readedBits == dictionary.endOfInformationCode) {
						freeDictionary(&dictionary);
						return EXIT_SUCCESS;
					}
					else {
						fprintf(stderr, "%s", "Incorrect gif file.");
						freeDictionary(&dictionary);
						return EXIT_FAILURE;
					}
				}


				// Clear code
				if (readedBits == dictionary.clearCode) {
					// Init dictionary
					if(reInitDictionary (&dictionary, reader)) {
						freeDictionary(&dictionary);
						return EXIT_FAILURE;
					}
				}
				else if (readedBits == dictionary.endOfInformationCode) {
					fprintf(stderr, "%s", "Incorrect gif file.");
					freeDictionary(&dictionary);
					return EXIT_FAILURE;
				}
				// Look into dictionary
				else {
					// Check the existence of the code in the dictionary
					if (codeInDictionary(&dictionary, &readedBits)) { // Code is not in dictionary

						// Get first index of CODE-1
						K = dictionary.colors[dictionary.previousCode].first->colorTableIndex;

						// Process CODE-1 record
						processColorList(&(dictionary.colors[dictionary.previousCode]), bmpOutputBuffer, reader->activeColorTable, bmpWriter);

						// List length pixels processed
						processedPixels += listLength(&(dictionary.colors[dictionary.previousCode]));

						// Process K
						processColor(K, bmpOutputBuffer, reader->activeColorTable, bmpWriter);

						// Pixel processed
						processedPixels++;

					}
					else {// Code is already in dictionary

						// Process CODE record
						processColorList(&(dictionary.colors[readedBits]), bmpOutputBuffer, reader->activeColorTable, bmpWriter);

						// List length pixels processed
						processedPixels += listLength(&(dictionary.colors[readedBits]));

						// Get first index of code
						K = dictionary.colors[readedBits].first->colorTableIndex;
					}

					// Create new dictionary record from CODE-1 and K
					if (copyLists(&(dictionary.colors[dictionary.previousCode]), &(dictionary.colors[dictionary.firstEmptyCode]))) {
						freeDictionary(&dictionary);
						return EXIT_FAILURE;
					}

					// Empty list
					if (dictionary.colors[dictionary.firstEmptyCode].last == NULL) {
						dictionary.colors[dictionary.firstEmptyCode].last = newDicItem(K);
						dictionary.colors[dictionary.firstEmptyCode].first = dictionary.colors[dictionary.firstEmptyCode].last;
					}
					else {
						dictionary.colors[dictionary.firstEmptyCode].last->nextColor = newDicItem(K);
						if (dictionary.colors[dictionary.firstEmptyCode].last->nextColor == NULL) {
							freeDictionary(&dictionary);
							return EXIT_FAILURE;
						}
						dictionary.colors[dictionary.firstEmptyCode].last = dictionary.colors[dictionary.firstEmptyCode].last->nextColor;
					}

					// Check dictionary capacity
					dictionary.firstEmptyCode++;

					// Increase LZW size
					if (dictionary.firstEmptyCode > dictionary.curMaxCode) {
						if (reader->lzwSize < 12) {
							reader->lzwSize++;
							switch(reader->lzwSize) {
								case(2):
										dictionary.curMaxCode = _2_BITS_MAX_CODE;
										break;
								case(3):
										dictionary.curMaxCode = _3_BITS_MAX_CODE;
										break;
								case(4):
										dictionary.curMaxCode = _4_BITS_MAX_CODE;
										break;
								case(5):
										dictionary.curMaxCode = _5_BITS_MAX_CODE;
										break;
								case(6):
										dictionary.curMaxCode = _6_BITS_MAX_CODE;
										break;
								case(7):
										dictionary.curMaxCode = _7_BITS_MAX_CODE;
										break;
								case(8):
										dictionary.curMaxCode = _8_BITS_MAX_CODE;
										break;
								case(9):
										dictionary.curMaxCode = _9_BITS_MAX_CODE;
										break;
								case(10):
										dictionary.curMaxCode = _10_BITS_MAX_CODE;
										break;
								case(11):
										dictionary.curMaxCode = _11_BITS_MAX_CODE;
										break;
								case(12):
										dictionary.curMaxCode = _12_BITS_MAX_CODE;
										break;
								default:
										fprintf(stderr, "%s", "Unsupported LZW size.");
										freeDictionary(&dictionary);
										return EXIT_FAILURE;
							}
						}
						// Dictionary is full - next code must be CC
						else
							dictionary.firstEmptyCode = DICTIONARY_FULL;
					}
				}
				// Set CODE-1
				dictionary.previousCode = readedBits;
			}
			break;
		}

		// Get data sub block
		while (reader->BytesToReadInSubBlock != 0) {

			// Normal mode
			if (firstCodeAfterCC == 0) {

				// Read code form block
				if (readBitsStreamFromFile (inputFile, reader, &readedBits, subBlockStatus)) {
					freeDictionary(&dictionary);
					return EXIT_FAILURE;
				}

				// Change block status
				subBlockStatus = UNFINISHED_SUBBLOCK;

				//printf("code: %d, prev code: %d, lzw size %d, dic max: %d, processedPixels: %d\n", readedBits, dictionary.previousCode, reader->lzwSize, dictionary.curMaxCode, processedPixels);
				//fflush(stdout);

				// Full dictionary?
				if (dictionary.firstEmptyCode == DICTIONARY_FULL) {

					// Expecting clean code
					if (readedBits == dictionary.clearCode) {

						// Restart process
						reader->lzwSize = reader->initLzwSize;
						firstCodeAfterCC = 1;

						// Init dictionary
						if(reInitDictionary(&dictionary, reader)) {
							freeDictionary(&dictionary);
							return EXIT_FAILURE;
						}
						continue;
					}
					// Wrong gif format
					else {
						fprintf(stderr, "%s", "Incorrect gif file.");
						freeDictionary(&dictionary);
						return EXIT_FAILURE;
					}
				}

				// Last code in data sub block
				if (readedBits == dictionary.endOfInformationCode) {
					freeDictionary(&dictionary);
					return EXIT_SUCCESS;
				}

				// Clear code
				else if (readedBits == dictionary.clearCode) {
					// Init dictionary
					if(reInitDictionary (&dictionary, reader)) {
						freeDictionary(&dictionary);
						return EXIT_FAILURE;
					}

				}
				// Look into dictionary
				else {
					// Check the existence of the code in the dictionary
					if (codeInDictionary(&dictionary, &readedBits)) { // Code is not in dictionary

						// Get first index of CODE-1
						K = dictionary.colors[dictionary.previousCode].first->colorTableIndex;

						// Process CODE-1 record
						processColorList(&(dictionary.colors[dictionary.previousCode]), bmpOutputBuffer, reader->activeColorTable, bmpWriter);

						// List length pixels processed
						processedPixels += listLength(&(dictionary.colors[dictionary.previousCode]));

						// Process K
						processColor(K, bmpOutputBuffer, reader->activeColorTable, bmpWriter);

						// Pixel processed
						processedPixels++;

					}
					else {// Code is already in dictionary

						// Process CODE record
						processColorList(&(dictionary.colors[readedBits]), bmpOutputBuffer, reader->activeColorTable, bmpWriter);

						// List length pixels processed
						processedPixels += listLength(&(dictionary.colors[readedBits]));

						// Get first index of code
						K = dictionary.colors[readedBits].first->colorTableIndex;
					}

					// Create new dictionary record from CODE-1 and K
					if (copyLists(&(dictionary.colors[dictionary.previousCode]), &(dictionary.colors[dictionary.firstEmptyCode]))) {
						freeDictionary(&dictionary);
						return EXIT_FAILURE;
					}

					// Empty list
					if (dictionary.colors[dictionary.firstEmptyCode].last == NULL) {
						dictionary.colors[dictionary.firstEmptyCode].last = newDicItem(K);
						dictionary.colors[dictionary.firstEmptyCode].first = dictionary.colors[dictionary.firstEmptyCode].last;
					}
					else {
						dictionary.colors[dictionary.firstEmptyCode].last->nextColor = newDicItem(K);
						if (dictionary.colors[dictionary.firstEmptyCode].last->nextColor == NULL) {
							freeDictionary(&dictionary);
							return EXIT_FAILURE;
						}
						dictionary.colors[dictionary.firstEmptyCode].last = dictionary.colors[dictionary.firstEmptyCode].last->nextColor;
					}

					// Check dictionary capacity
					dictionary.firstEmptyCode++;

					// Increase LZW size
					if (dictionary.firstEmptyCode > dictionary.curMaxCode) {
						if (reader->lzwSize < 12) {
							reader->lzwSize++;
							switch(reader->lzwSize) {
								case(2):
										dictionary.curMaxCode = _2_BITS_MAX_CODE;
										break;
								case(3):
										dictionary.curMaxCode = _3_BITS_MAX_CODE;
										break;
								case(4):
										dictionary.curMaxCode = _4_BITS_MAX_CODE;
										break;
								case(5):
										dictionary.curMaxCode = _5_BITS_MAX_CODE;
										break;
								case(6):
										dictionary.curMaxCode = _6_BITS_MAX_CODE;
										break;
								case(7):
										dictionary.curMaxCode = _7_BITS_MAX_CODE;
										break;
								case(8):
										dictionary.curMaxCode = _8_BITS_MAX_CODE;
										break;
								case(9):
										dictionary.curMaxCode = _9_BITS_MAX_CODE;
										break;
								case(10):
										dictionary.curMaxCode = _10_BITS_MAX_CODE;
										break;
								case(11):
										dictionary.curMaxCode = _11_BITS_MAX_CODE;
										break;
								case(12):
										dictionary.curMaxCode = _12_BITS_MAX_CODE;
										break;
								default:
										fprintf(stderr, "%s", "Unsupported LZW size.");
										freeDictionary(&dictionary);
										return EXIT_FAILURE;
							}
						}
						// Dictionary is full - next code must be CC
						else
							dictionary.firstEmptyCode = DICTIONARY_FULL;
					}
				}
				// Set CODE-1
				dictionary.previousCode = readedBits;
			}
			else { // Fill reader

				// Normal mode for reader
				firstCodeAfterCC = 0;

				// Read code form block
				if (readBitsStreamFromFile (inputFile, reader, &readedBits, subBlockStatus)) {
					freeDictionary(&dictionary);
					return EXIT_FAILURE;
				}

				//printf("code: %d, prev code: %d, lzw size %d, dic max: %d, processedPixels: %d\n", readedBits, dictionary.previousCode, reader->lzwSize, dictionary.curMaxCode, processedPixels);
				//fflush(stdout);

				// Last code in data sub block
				if (readedBits == dictionary.endOfInformationCode) {
					freeDictionary(&dictionary);
					return EXIT_SUCCESS;
				}

				// Clear code
				else if (readedBits == dictionary.clearCode) {
					// Init dictionary
					if(reInitDictionary (&dictionary, reader)) {
						freeDictionary(&dictionary);
						return EXIT_FAILURE;
					}
				}
				// Should be in dictionary
				else {
					// Check the existence of the code in the dictionary
					if (codeInDictionary(&dictionary, &readedBits)) {
						fprintf(stderr, "%s", "Incorrect gif file.");
						freeDictionary(&dictionary);
						return EXIT_FAILURE;
					}
					else {
						processColor(readedBits, bmpOutputBuffer, reader->activeColorTable, bmpWriter);

						// Pixel processed
						processedPixels++;

						// Set CODE-1
						dictionary.previousCode = readedBits;
					}
				}
			}
		}

	}

	freeDictionary(&dictionary);

	return EXIT_SUCCESS;
}

/**
 * Function read color table from GIF file and save it into field
 *
 * @param gifFile Input GIF file
 * @param colorTable Color table field
 * @param colorTableSize Size of color table
 * @return 0 on success, 1 on failure
 */
int getColorTable(FILE *gifFile, tRGB colorTable [], int colorTableSize) {

	u_int8_t Byte = 0;
	int readRetVal = 0;

	for (int i = 0; i < colorTableSize; i++) {
		for (int j = 0; j < 3; j++) {
			readRetVal = readByteFromFile(gifFile, &Byte);
			if (readRetVal == READ_WRITE_ERR) {
				fprintf(stderr, "%s", "Can not read input file.");
				return EXIT_FAILURE;
			}
			else if (readRetVal == END_OF_FILE) {
				fprintf(stderr, "%s", "Incorrect gif file.");
				return EXIT_FAILURE;
			}
			// Save colors
			else {
				if (j == 0)
					colorTable[i].red = Byte;
				else if (j == 1)
					colorTable[i].green = Byte;
				else
					colorTable[i].blue = Byte;
			}
		}
	}
	return EXIT_SUCCESS;
}

/**
 * Function read application extension, only skip values
 *
 * @param inputFile Input GIF file
 * @return 0 on success, 1 on failure
 */
int getApplicationExt(FILE *inputFile) {

	u_int8_t Byte = 0;
	int readRetVal;

	readRetVal = readByteFromFile(inputFile, &Byte);
	if (readRetVal == READ_WRITE_ERR) {
		fprintf(stderr, "%s", "Can not read input file.");
		return EXIT_FAILURE;
	}
	else if (readRetVal == END_OF_FILE) {
		fprintf(stderr, "%s", "Incorrect gif file.");
		return EXIT_FAILURE;
	}
	else {
		// get extension size
		int appliExtSize = (int)Byte;

		// fixed value
		if (appliExtSize != 11) {
			fprintf(stderr, "%s", "Incorrect gif file.");
			return EXIT_FAILURE;
		}

		// read application identifier and authentication code and block size (last readed byte)
		for (int i = 0; i < appliExtSize + 1; i++) {
			readRetVal = readByteFromFile(inputFile, &Byte);
			if (readRetVal == READ_WRITE_ERR) {
				fprintf(stderr, "%s", "Can not read input file.");
				return EXIT_FAILURE;
			}
			else if (readRetVal == END_OF_FILE) {
				fprintf(stderr, "%s", "Incorrect gif file.");
				return EXIT_FAILURE;
			}
			else {
				// SKIP VALUES
			}
		}
		// get data block size
		appliExtSize = (int)Byte;

		// read data block, last byte must be empty sub block
		for (int i = 0; i < appliExtSize + 1; i++) {
			readRetVal = readByteFromFile(inputFile, &Byte);
			if (readRetVal == READ_WRITE_ERR) {
				fprintf(stderr, "%s", "Can not read input file.");
				return EXIT_FAILURE;
			}
			else if (readRetVal == END_OF_FILE) {
				fprintf(stderr, "%s", "Incorrect gif file.");
				return EXIT_FAILURE;
			}
			else {
				// SKIP VALUES
			}
		}

		// check last byte value
		if (Byte != BLOCK_TERMINATOR) {
			fprintf(stderr, "%s", "Incorrect gif file.");
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

/**
 * Function read comment extension, only skip values
 *
 * @param inputFile Input GIF file
 * @return 0 on success, 1 on failure
 */
int getCommentExt(FILE *inputFile) {

	u_int8_t Byte = 0;
	int readRetVal;

	readRetVal = readByteFromFile(inputFile, &Byte);
	if (readRetVal == READ_WRITE_ERR) {
		fprintf(stderr, "%s", "Can not read input file.");
		return EXIT_FAILURE;
	}
	else if (readRetVal == END_OF_FILE) {
		fprintf(stderr, "%s", "Incorrect gif file.");
		return EXIT_FAILURE;
	}
	else {
		// get extension size
		int extSize = (int)Byte;

		// read application identifier and authentication code and block size (last readed byte)
		for (int i = 0; i < extSize + 1; i++) {
			readRetVal = readByteFromFile(inputFile, &Byte);
			if (readRetVal == READ_WRITE_ERR) {
				fprintf(stderr, "%s", "Can not read input file.");
				return EXIT_FAILURE;
			}
			else if (readRetVal == END_OF_FILE) {
				fprintf(stderr, "%s", "Incorrect gif file.");
				return EXIT_FAILURE;
			}
			else {
				// SKIP VALUES
			}
		}

		// check last byte value
		if (Byte != BLOCK_TERMINATOR) {
			fprintf(stderr, "%s", "Incorrect gif file.");
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

/**
 * Function read plain text extension, only skip values
 *
 * @param inputFile Input GIF file
 * @return 0 on success, 1 on failure
 */
int getPlainTextExt(FILE *inputFile) {
	// TO DO
	return EXIT_SUCCESS;
}

/**
 * Function read graphic control extension, only skip values
 *
 * @param inputFile Input GIF file
 * @return 0 on success, 1 on failure
 */
int getGraphicControlExt(FILE *inputFile) {

	u_int8_t Byte = 0;
	int readRetVal;

	readRetVal = readByteFromFile(inputFile, &Byte);
	if (readRetVal == READ_WRITE_ERR) {
		fprintf(stderr, "%s", "Can not read input file.");
		return EXIT_FAILURE;
	}
	else if (readRetVal == END_OF_FILE) {
		fprintf(stderr, "%s", "Incorrect gif file.");
		return EXIT_FAILURE;
	}
	else {
		// get extension size
		int extSize = (int)Byte;

		// check extension size - fixed value
		if (Byte != GRAPHICS_CONTROL_EXTENSION_SIZE) {
			fprintf(stderr, "%s", "Incorrect gif file.");
			return EXIT_FAILURE;
		}

		// read application identifier and authentication code and block size (last readed byte)
		for (int i = 0; i < extSize + 1; i++) {
			readRetVal = readByteFromFile(inputFile, &Byte);
			if (readRetVal == READ_WRITE_ERR) {
				fprintf(stderr, "%s", "Can not read input file.");
				return EXIT_FAILURE;
			}
			else if (readRetVal == END_OF_FILE) {
				fprintf(stderr, "%s", "Incorrect gif file.");
				return EXIT_FAILURE;
			}
			else {
				// SKIP VALUES
			}
		}

		// check last byte value
		if (Byte != BLOCK_TERMINATOR) {
			fprintf(stderr, "%s", "Incorrect gif file.");
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

/**
 * Function read image description and save image property into struct
 *
 * @param inputFile Input GIF file
 * @param imageDescriptor Struct for image property
 * @return 0 on success, 1 on failure
 */
int getImageDescriptor(FILE *gifFile, tIMAGE_DESCRIPTOR *imageDescriptor) {
	u_int8_t Byte = 0;
	int readRetVal;
	int tmp = 0;

	// Get image descriptor block
	for (int i = 0; i < IMAGE_DESCRIPTOR_SIZE - 1; i++) {
		readRetVal = readByteFromFile(gifFile, &Byte);
		if (readRetVal == READ_WRITE_ERR) {
			fprintf(stderr, "%s", "Can not read input file.");
			return EXIT_FAILURE;
		}
		else if (readRetVal == END_OF_FILE) {
			fprintf(stderr, "%s", "Incorrect gif file.");
			return EXIT_FAILURE;
		}
		else {
			// Save pic property
			switch (i) {
				case (0):
						imageDescriptor->leftPosLowByte = Byte;
						break;
				case (1):
						imageDescriptor->leftPosHighByte = Byte;
						break;
				case (2):
						imageDescriptor->topPosLowByte = Byte;
						break;
				case (3):
						imageDescriptor->topPosHighByte = Byte;
						break;
				case (4):
						imageDescriptor->widthLowByte = Byte;
						break;
				case (5):
						imageDescriptor->widthHighByte = Byte;
						break;
				case (6):
						imageDescriptor->heightLowByte = Byte;
						break;
				case (7):
						imageDescriptor->heightHighByte = Byte;
						break;
				case (8):
						imageDescriptor->localColorTableFlag = (u_int8_t)((Byte & GIFMASK_LOCAL_COLOR_PALETTE) >> 7 );
						imageDescriptor->interlaceFlag = (u_int8_t)((Byte & GIFMASK_LOCAL_INTERLACE) >> 6);
						imageDescriptor->sortFlag = (u_int8_t)((Byte & GIFMASK_LOCAL_COLOR_PALETTE_SORT) >> 5);
						tmp = 2;
						for (int j = 0; j < (int)(Byte & GIFMASK_LOCAL_COLOR_PALETTE_SIZE); j++)
							tmp = tmp*2;
						imageDescriptor->localColorTableSize = tmp;
						break;
				}
		}
	}

	// get data block size in pixels
	imageDescriptor->sizeInPixels = (imageDescriptor->widthHighByte*256 + imageDescriptor->widthLowByte) * (imageDescriptor->heightHighByte*256 + imageDescriptor->heightLowByte);

	return EXIT_SUCCESS;
}

/**
 * Function check GIF file version, supported is 89a
 *
 * @param inputFile Input GIF file
 * @return 0 on success (supported GIF version), 1 on failure
 */
int checkGifVersion(FILE *gifFile) {

	u_int8_t Byte = 0;
	int readRetVal;

	for (int i = 0; i < 6; i++) {
		readRetVal = readByteFromFile(gifFile, &Byte);
		if (readRetVal == READ_WRITE_ERR) {
			fprintf(stderr, "%s", "Can not read input file.");
			return EXIT_FAILURE;
		}
		else if (readRetVal == END_OF_FILE) {
			fprintf(stderr, "%s", "Incorrect gif file.");
			return EXIT_FAILURE;
		}
		else {
			switch (i) {
				case (0): // G
						if (Byte != 0x47) {
							fprintf(stderr, "%s", "Incorrect gif file.");
							return EXIT_FAILURE;
						}
						break;
				case (1):// I
						if (Byte != 0x49) {
							fprintf(stderr, "%s", "Incorrect gif file.");
							return EXIT_FAILURE;
						}
						break;
				case (2):// F
						if (Byte != 0x46) {
							fprintf(stderr, "%s", "Incorrect gif file.");
							return EXIT_FAILURE;
						}
						break;
				case (3):// 8
						if (Byte != 0x38) {
							fprintf(stderr, "%s", "Incorrect gif file.");
							return EXIT_FAILURE;
						}
						break;
				case (4):// 9
						if (Byte != 0x39) {
							fprintf(stderr, "%s", "Incorrect gif file.");
							return EXIT_FAILURE;
						}
						break;
				case (5):// a
						if (Byte != 0x61) {
							fprintf(stderr, "%s", "Incorrect gif file.");
							return EXIT_FAILURE;
						}
						break;
			}
		}
	}

	return EXIT_SUCCESS;
}

/**
 * Function read GIF header and save it into pic property struct
 *
 * @param gifFile Input GIF file
 * @param pic Picture property struct
 * @return 0 on success (supported GIF version), 1 on failure
 */
int parseGifHeader(FILE *gifFile, tPIC_PROPERTY *pic) {

	u_int8_t Byte = 0;
	int readRetVal;
	int tmp = 0;

	for (int i = 0; i < 7; i++) {
		readRetVal = readByteFromFile(gifFile, &Byte);
		if (readRetVal == READ_WRITE_ERR) {
			fprintf(stderr, "%s", "Can not read input file.");
			return EXIT_FAILURE;
		}
		else if (readRetVal == END_OF_FILE) {
			fprintf(stderr, "%s", "Incorrect gif file.");
			return EXIT_FAILURE;
		}
		else {
			switch (i) {
				case (0):
						pic->widthInPixLowByte = Byte;
						break;
				case (1):
						pic->widthInPixHighByte = Byte;
						break;
				case (2):
						pic->heightInPixLowByte = Byte;
						break;
				case (3):
						pic->heightInPixHighByte = Byte;
						break;
				case (4):
						pic->globalColorTable = (u_int8_t)((Byte & GIFMASK_GLOBAL_COLOR_PALETTE) >> 7 );
						pic->bitsPerPixel = ((u_int8_t)(((Byte & GIFMASK_COLOR_BITS_PER_PIXEL) >> 4 )) + 1);
						if (pic->bitsPerPixel != 8) {
							fprintf(stderr, "%s", "Unsupported color width.");
							return EXIT_FAILURE;
						}
						pic->colorTableSorted = (u_int8_t)((Byte & GIFMASK_COLOR_PALETTE_SORT) >> 3);
						tmp = 2;
						for (int j = 0; j < (int)(Byte & GIFMASK_GLOBAL_COLOR_PALETTE_SIZE); j++)
							tmp = tmp*2;
						pic->colorTableLong = tmp;
						break;
				case (5):
						pic->backgroundColor = Byte;
						break;
				case (6):
						pic->pixelAspectRatio = Byte;
						break;
			}
		}
	}
	return EXIT_SUCCESS;
}
