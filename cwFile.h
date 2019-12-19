#ifndef cwFile_H
#define cwFile_H

namespace cw
{
  typedef handle<struct file_str> fileH_t;

  // Flags for use with fileOpen().
  enum fileOpenFlags_t
  {
    kReadFileFl   = 0x01, //< Open a file for reading
    kWriteFileFl  = 0x02, //< Create an empty file for writing
    kAppendFileFl = 0x04, //< Open a file for writing at the end of the file.
    kUpdateFileFl = 0x08, //< Open a file for reading and writing.
    kBinaryFileFl = 0x10, //< Open a file for binary (not text) input/output.
    kStdoutFileFl = 0x20, //< Ignore fn use 'stdout'
    kStderrFileFl = 0x40, //< Ignore fn use 'stderr'
    kStdinFileFl  = 0x80, //< Ignore fn use 'stdin'
  };

  // Open or create a file.    
  // Equivalent to fopen().
  // If *hp was not initalized by an earlier call to fileOpen() then it should 
  // be set to fileNullHandle prior to calling this function. If *hp is a valid handle
  // then it is automatically finalized by an internal call to fileClose() prior to
  // being re-iniitalized.
  //
  // If kStdoutFileFl, kStderrFileFl or kStdinFileFl are set then 
  // file name argument 'fn' is ignored.
  rc_t fileOpen(    
    fileH_t&    hRef,           // Pointer to a client supplied fileHandle_t to recieve the handle for the new object.
    const char* fn,             // The name of the file to open or create. 
    unsigned    flags );        // See fileOpenFlags_t

  // Close a file opened with  Equivalent to fclose().
  rc_t fileClose(   fileH_t& hRef );

  // Return true if the file handle is associated with an open file.
  bool       fileIsValid( fileH_t h );

  // Read a block bytes from a file. Equivalent to fread().
  rc_t fileRead(    fileH_t h, void* buf, unsigned bufByteCnt );

  // Write a block of bytes to a file. Equivalent to fwrite().
  rc_t fileWrite(   fileH_t h, const void* buf, unsigned bufByteCnt );
  
  enum fileSeekFlags_t 
  { 
    kBeginFileFl = 0x01, 
    kCurFileFl   = 0x02, 
    kEndFileFl   = 0x04 
  };

  // Set the file position indicator. Equivalent to fseek().
  rc_t fileSeek(    fileH_t h, enum fileSeekFlags_t flags, int offsByteCnt );

  // Return the file position indicator. Equivalent to ftell().
  rc_t fileTell(    fileH_t h, long* offsPtr );

  // Return true if the file position indicator is at the end of the file. 
  // Equivalent to feof().
  bool       fileEof(     fileH_t h );

  // Return the length of the file in bytes
  unsigned   fileByteCount(  fileH_t h );
  rc_t fileByteCountFn( const char* fn, unsigned* fileByteCntPtr );

  // Set *isEqualPtr=true if the two files are identical.
  rc_t fileCompare( const char* fn0, const char* fn1, bool& isEqualFlRef );

  // Return the file name associated with a file handle.
  const char* fileName( fileH_t h );

  // Write a buffer to a file.
  rc_t fileFnWrite( const char* fn, const void* buf, unsigned bufByteCnt );

  // Allocate and fill a buffer from the file.
  // Set *bufByteCntPtr to count of bytes read into the buffer.
  // 'bufByteCntPtr' is optional - set it to nullptr if it is not required by the caller.
  // It is the callers responsibility to delete the returned buffer with a
  // call to cmMemFree()
  char*  fileToBuf( fileH_t h, unsigned* bufByteCntPtr ); 
  
  // Same as fileToBuf() but accepts a file name argument.
  char*  fileFnToBuf( const char* fn, unsigned* bufByteCntPtr );


  // Copy the file named in srcDir/srcFn/srcExt to a file named dstDir/dstFn/dstExt.
  // Note that srcExt/dstExt may be set to nullptr if the file extension is included
  // in srcFn/dstFn.  Likewise srcFn/dstFn may be set to nullptr if the file name
  // is included in srcDir/dstDir.
  rc_t    fileCopy( 
    const char* srcDir, 
    const char* srcFn, 
    const char* srcExt, 
    const char* dstDir, 
    const char* dstFn, 
    const char* dstExt);


  // This function creates a backup copy of the file 'fn' by duplicating it into
  // a file named fn_#.ext where # is an integer which makes the file name unique.
  // The integers chosen with zero and are incremented until an
  // unused file name is found in the same directory as 'fn'.
  // If the file identified by 'fn' is not found then the function returns quietly.
  rc_t fileBackup( const char* dir, const char* name, const char* ext );
  
  // Allocate and fill a zero terminated string from a file.
  // Set *bufByteCntPtr to count of bytes read into the buffer.=
  // (the buffer memory size is one byte larger to account for the terminating zero) 
  // 'bufByteCntPtr' is optional - set it to nullptr if it is not required by the caller.
  // It is the callers responsibility to delete the returned buffer with a
  // call to cmMemFree()
  char*  fileToStr( fileH_t h, unsigned* bufByteCntPtr );

  // Same as fileToBuf() but accepts a file name argument.
  char*  fileFnToStr( const char* fn, unsigned* bufByteCntPtr );

  // Return the count of lines in a file.
  rc_t fileLineCount( fileH_t h, unsigned* lineCntPtr );

  // Read the next line into buf[bufByteCnt]. 
  // Consider using fileGetLineAuto() as an alternative to this function 
  // to avoid having to use a buffer with an explicit size.
  //
  // If buf is not long enough to hold the entire string then
  //
  // 1. The function returns kFileBufTooSmallRC
  // 2. *bufByteCntPtr is set to the size of the required buffer.
  // 3. The internal file position is left unchanged.
  //
  // If the buffer is long enough to hold the entire line then 
  // *bufByteCntPtr is left unchanged.
  // See  fileGetLineTest() in cmProcTest.c or fileGetLineAuto()
  // in file.c for examples of how to use this function to a
  // achieve proper buffer sizing.
  rc_t fileGetLine( fileH_t h, char* buf, unsigned* bufByteCntPtr );

  // A version of fileGetLine() which eliminates the need to handle buffer
  // sizing. 
  //
  // Example usage:
  // 
  // char* buf        = nullptr;
  // unsigned  bufByteCnt = 0;
  // while(fileGetLineAuto(h,&buf,&bufByteCnt)==kOkFileRC)
  //   proc(buf);
  // cmMemPtrFree(buf);
  //
  // On the first call to this function *bufPtrPtr must be set to nullptr and
  // *bufByteCntPtr must be set to 0.
  // Following the last call to this function call cmMemPtrFree(bufPtrptr)
  // to be sure the line buffer is fully released. Note this step is not
  // neccessary if the last call does not return kOkFileRC.
  rc_t fileGetLineAuto( fileH_t h, char** bufPtrPtr, unsigned* bufByteCntPtr );

  // Binary Array Reading Functions
  // Each of these functions reads a block of binary data from a file.
  // The advantage to using these functions over fileRead() is only that they are type specific.
  rc_t fileReadChar(   fileH_t h, char*           buf, unsigned cnt );
  rc_t fileReadUChar(  fileH_t h, unsigned char*  buf, unsigned cnt );
  rc_t fileReadShort(  fileH_t h, short*          buf, unsigned cnt );
  rc_t fileReadUShort( fileH_t h, unsigned short* buf, unsigned cnt );
  rc_t fileReadLong(   fileH_t h, long*           buf, unsigned cnt );
  rc_t fileReadULong(  fileH_t h, unsigned long*  buf, unsigned cnt );
  rc_t fileReadInt(    fileH_t h, int*            buf, unsigned cnt );
  rc_t fileReadUInt(   fileH_t h, unsigned int*   buf, unsigned cnt );
  rc_t fileReadFloat(  fileH_t h, float*          buf, unsigned cnt );
  rc_t fileReadDouble( fileH_t h, double*         buf, unsigned cnt );
  rc_t fileReadBool(   fileH_t h, bool*           buf, unsigned cnt );

  // Binary Array Writing Functions
  // Each of these functions writes an array to a binary file.
  // The advantage to using functions rather than fileWrite() is only that they are type specific.
  rc_t fileWriteChar(   fileH_t h, const char*           buf, unsigned cnt );
  rc_t fileWriteUChar(  fileH_t h, const unsigned char*  buf, unsigned cnt );
  rc_t fileWriteShort(  fileH_t h, const short*          buf, unsigned cnt );
  rc_t fileWriteUShort( fileH_t h, const unsigned short* buf, unsigned cnt );
  rc_t fileWriteLong(   fileH_t h, const long*           buf, unsigned cnt );
  rc_t fileWriteULong(  fileH_t h, const unsigned long*  buf, unsigned cnt );
  rc_t fileWriteInt(    fileH_t h, const int*            buf, unsigned cnt );
  rc_t fileWriteUInt(   fileH_t h, const unsigned int*   buf, unsigned cnt );
  rc_t fileWriteFloat(  fileH_t h, const float*          buf, unsigned cnt );
  rc_t fileWriteDouble( fileH_t h, const double*         buf, unsigned cnt );
  rc_t fileWriteBool(   fileH_t h, const bool*           buf, unsigned cnt );

  // Write a string to a file as <N> <char0> <char1> ... <char(N-1)>
  // where N is the count of characters in the string.
  rc_t fileWriteStr( fileH_t h, const char* s );

  // Read a string back from a file as written by fileWriteStr().
  // Note that the string will by string will be dynamically allocated
  // and threfore must eventually be released via cmMemFree().
  // If maxCharN is set to zero then the default maximum string
  // length is 16384.  Note that this limit is used to prevent
  // corrupt files from generating excessively long strings.
  rc_t fileReadStr(  fileH_t h, char** sRef, unsigned maxCharN );

  // Formatted Text Output Functions:
  // Print formatted text to a file.
  rc_t filePrint(   fileH_t h, const char* text );
  rc_t filePrintf(  fileH_t h, const char* fmt, ... );
  rc_t fileVPrintf( fileH_t h, const char* fmt, va_list vl );
  

  
}




#endif
