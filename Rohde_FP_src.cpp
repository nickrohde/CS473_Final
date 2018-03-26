#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <Windows.h>
#include <fstream>
#include <string>
#include <omp.h>
#include <chrono>

using namespace std;

typedef unsigned int uint;

// Defines:
#define NUM_THREADS 16        // Number of threads running
#define ALPHABET_SIZE 256     // ASCII Alphabet


// Macros:
#ifndef MAX
#define MAX(a,b) ((a > b) ? a : b)
#endif

#ifndef MIN
#define MIN(a,b) ((a < b) ? a : b)
#endif

#define GET_OVERLAP(a,b) (a + ((b-1)*2))


// Globals:
int  ia_shift_table[ALPHABET_SIZE];


// Setup the Horspool shift table
void setupShiftTable(const char* s_pattern, const size_t ui_pattern_length)
{
	int i = 0;

	// Initialize table as not present in pattern
	#pragma omp parallel for
	for (i = 0; i < ALPHABET_SIZE; i++)
	{
		ia_shift_table[i] = -1;
	} // end for

	// Fill the actual index of characters in the pattern
	#pragma omp parallel for private(i)
	for (i = 0; i < ui_pattern_length; i++)
	{
		ia_shift_table[static_cast<int>(s_pattern[i])] = i;
	} // end for
} // end method setupShiftTable


// Searches the given text for the given pattern and increments the global counter variable accordingly
uint search(const char* s_text, const size_t ui_TEXT_LENGTH, const char* s_pattern, const size_t ui_PATTERN_LENGTH)
{
	int i_shiftLocation = 0;  // current position in the text

	uint ui_counter = 0;     // number of occurrences of the pattern in the text

	if (ui_TEXT_LENGTH <= 0)
	{
		return 0;
	} // end if

	while (i_shiftLocation <= (ui_TEXT_LENGTH - ui_PATTERN_LENGTH))
	{
		int i_matchIndex = ui_PATTERN_LENGTH - 1;

		// move along the pattern until a unmatching char is found, or it becomes -1
		while (i_matchIndex >= 0 && s_pattern[i_matchIndex] == s_text[i_shiftLocation + i_matchIndex])
		{
			i_matchIndex--;
		} // end while
		
		// if the above loop produced an idex of -1, then the entire pattern was matched
		if (i_matchIndex < 0)
		{
			ui_counter++; // found an occurrence of the pattern

			// move the shift to the next character, but do not go over the length of the text
			i_shiftLocation += (i_shiftLocation + ui_PATTERN_LENGTH < ui_TEXT_LENGTH) ? ui_PATTERN_LENGTH - ia_shift_table[static_cast<int>(s_text[i_shiftLocation + ui_PATTERN_LENGTH])] : 1;

			// move the shift to the next match in the pattern, but do not go over the length of the text
			if ((i_shiftLocation + ui_PATTERN_LENGTH) < ui_TEXT_LENGTH)
			{
				i_shiftLocation += ui_PATTERN_LENGTH - ia_shift_table[static_cast<int>(s_text[i_shiftLocation + ui_PATTERN_LENGTH])];
			} // end if
			else // we are at the end of the text, brute force search the remaining bytes
			{
				i_shiftLocation += 1;
			} // end else
		} // end if
		else
		{
			// shift the pattern to next matching character in text
			i_shiftLocation += MAX(1, i_matchIndex - ia_shift_table[s_text[i_shiftLocation + i_matchIndex]]);
		} // end else
	} // end while

	return ui_counter;
} // end method search


// Prints a message alerting them of an argument issue during start up
void printArgError(const char * s_name)
{
	cout << "Arg 1: File containing the text to search." << endl;
	cout << "Arg 2: File containing the pattern to search for." << endl;
	cout << "Arg 3 (Optional): Number of threads to use (Positive Integer)." << endl;
	cout << "Example call: " << s_name << " text.txt pattern.txt <number of threads>" << endl << endl;
} // end method printArgError


// Setup method which initializes all necessary parts
int main(int argc, char ** argv)
{	
	// Variables:
	char*     s_pattern         = NULL;   // the pattern
	char**    sa_chunks         = NULL;   // the text
	
	uint	  ui_result         = 0; // final result being printed to console

	size_t    ui_textLength     = 0, // length of the text to search
		  ui_patternLength  = 0, // length of the pattern to find
		  ui_bytesToProcess = 0, // number of bytes left to process
		  ui_chunkSize      = 0, // size of chunks of new data to extract from text
		  ui_bufferSize     = 0, // actual size of buffer accounting for overlap
		  ui_numberOfChunks = 0, // number of chunks the text is divided into
		  ui_numThreads     = 0, // number of threads being used to run the algorithm
		  ui_overlap        = 0, // size of the overlap between chunks
		  ui_firstLength    = 0, // size of the first chunk
		  ui_lastLength     = 0; // size of the last  chunk

	// Argument error handling
	if (argc < 3)
	{
		cout << "You supplied too few arguments. You must provide either 2 or 3 arguments. Exiting ..." << endl;
		printArgError(argv[0]);
		exit(EXIT_FAILURE);
	} // end if
	else if (argc == 3)
	{
		ui_numThreads = NUM_THREADS;
	} // end elif
	else if (argc == 4)
	{
		int temp= -1;
		try
		{
			temp = atoi(argv[3]);

			if (temp > 0)
			{
				ui_numThreads = static_cast<size_t>(temp);
			} // end if
			else
			{
				cout << "You supplied an invalid argument! Exiting ..." << endl;
				printArgError(argv[0]);
				exit(EXIT_FAILURE);
			} // end else
		} // end try
		catch(exception e)
		{
			cout << "You supplied an invalid argument! Exiting ..." << endl;
			printArgError(argv[0]);
			exit(EXIT_FAILURE);
		} // end catch
	} // end elif
	else
	{
		cout << "You supplied too many arguments! You must provide either 2 or 3 arguments. Exiting ..." << endl;
		printArgError(argv[0]);
		exit(EXIT_FAILURE);
	} // end elif

	// Files:
	ifstream file_text(argv[1]);
	ifstream file_pattern(argv[2]);

	int patternOffset;     // the offset for creating the overlap between threads

	if (file_pattern.bad() || !file_pattern.is_open())
	{
		cout << "ERROR! The pattern could not be opened. Exiting ..." << endl;
		file_text.close();
		file_results.close();
		exit(EXIT_FAILURE);
	} // end if
	else
	{	
		file_pattern.seekg(0, ios::end);
		ui_patternLength = file_pattern.tellg(); // get pattern length
		file_pattern.seekg(0, ios::beg);
		
		patternOffset = -1 * (ui_patternLength - 1); // offset for creating overlap 

		s_pattern = new char[ui_patternLength+1];

		file_pattern.read(s_pattern, ui_patternLength);

		s_pattern[ui_patternLength] = '\0';
		file_pattern.close();
	} // end else


	if (file_text.bad() || !file_text.is_open())
	{
		cout << "ERROR! The text could not be opened. Exiting ..." << endl;
		delete s_pattern;
		file_text.close();
		file_results.close();
		exit(EXIT_FAILURE);
	} // end if
	else
	{	
		file_text.seekg(0, ios::end);
		ui_textLength = file_text.tellg();
		ui_bytesToProcess = ui_textLength;
		file_text.seekg(0, ios::beg);

		if (ui_patternLength > ui_textLength)
		{
			cout << "The pattern is longer than the provided text. Exiting ..." << endl;
			delete s_pattern;
			file_text.close();
			file_results.close();
			exit(EXIT_SUCCESS);
		} // end if

		try
		{
			setupShiftTable(s_pattern, ui_patternLength);
		} // end try
		catch (exception e)
		{
			cout << "An exception occurred during shift table setup: " << e.what() << endl;
			delete s_pattern;
			file_text.close();
			file_results.close();
			exit(EXIT_FAILURE);
		} // end catch

		try
		{   
			ui_chunkSize       = ceil(static_cast<double>(static_cast<double>(ui_textLength) / static_cast<double>(ui_numThreads)));
			ui_overlap         = (2 * ui_patternLength) - 2;
			ui_bufferSize      = ui_chunkSize + ui_overlap;  
			ui_firstLength     = ui_chunkSize + ui_patternLength - 1;
			ui_numberOfChunks  = ceil(static_cast<double>(static_cast<double>(ui_textLength) / static_cast<double>(ui_chunkSize)));

			sa_chunks = new char*[ui_numberOfChunks];

			sa_chunks[0] = new char[ui_firstLength + 1]; // add 1 byte for terminator
			file_text.read(sa_chunks[0], ui_firstLength);
			sa_chunks[0][ui_firstLength] = '\0';
			ui_bytesToProcess -= ui_chunkSize;

			// central chunks get overlap on both sides
			for (int i = 1; i < ui_numberOfChunks - 1; i++)
			{
				file_text.seekg(ui_chunkSize * i, ios::beg); // go back (m-1) bytes for overlap
				
				ui_bytesToProcess -= ui_chunkSize;

				sa_chunks[i] = new char[ui_bufferSize + 1]; // add 1 byte for terminator

				file_text.read(sa_chunks[i], ui_bufferSize);
				sa_chunks[i][ui_bufferSize] = '\0'; 
			} // end for
			
			
			if (ui_bytesToProcess > ui_overlap)
			{
				file_text.seekg(ui_chunkSize * (ui_numberOfChunks - 1), ios::beg); // go back (m-1) bytes for overlap

				ui_lastLength = ui_bytesToProcess + ui_chunkSize; // length of last chunk depends on length of text

				sa_chunks[ui_numberOfChunks - 1] = new char[ui_lastLength + 1]; // add 1 byte for terminator
				file_text.read(sa_chunks[ui_numberOfChunks - 1], ui_lastLength);
				sa_chunks[ui_numberOfChunks - 1][ui_lastLength] = '\0';
			} // end if
			else if(ui_numberOfChunks > 1) // if the remaining bytes < the overlap, then the last string will just be a repeat of the of the the second to last, which can be ignored
			{                              // the if condition is there to avoid the array being destroyed if there is only 1 thread, in which case the first if statement will fail 
				sa_chunks[ui_numberOfChunks - 1] = NULL;
				ui_lastLength = 0;
			} // end elif

			size_t len = 0;

			omp_set_dynamic(0); // to avoid omp deciding the # threads to use

			// Get the text in chunks and distribute them to the threads
			#pragma omp parallel for private(len) reduction(+:ui_result) schedule(guided) num_threads(ui_numThreads)
			for (int i = 0; i < ui_numberOfChunks; i++)
			{		
				if (i == 0)
				{
					len = ui_firstLength; // first string only has one overlap on the right
				} // end if
				else if (i < (ui_numberOfChunks - 1))
				{
					len = ui_bufferSize; 
				} // end elif
				else
				{
					len = ui_lastLength; // last string might be much shorter
				} // end else

				ui_result += search(sa_chunks[i], len, s_pattern, ui_patternLength);
			} // end parallel for
			
			cout << "The pattern was found " << ui_result << " times in the text." << endl;

			for (size_t i = 0; i < ui_numberOfChunks; i++)
			{
				delete sa_chunks[i];
			} // end for
			delete sa_chunks;
			delete s_pattern;
			file_text.close();
		} // end try
		catch (exception e)
		{
			for (size_t i = 0; i < ui_numberOfChunks; i++)
			{
				delete sa_chunks[i];
			} // end for
			delete sa_chunks;
			delete s_pattern;
			file_text.close();
			exit(EXIT_FAILURE);
		} // end catch
	} // end else
	
	exit(EXIT_SUCCESS);
} // end Main                                                                                                          

