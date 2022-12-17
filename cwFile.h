#ifndef cwFile_H
#define cwFile_H

namespace cw
{
  namespace file
  {
    
    typedef handle<struct file_str> handle_t;

    // Flags for use with fileOpen().
    enum openFlags_t
    {
     kReadFl   = 0x01, //< Open a file for reading
     kWriteFl  = 0x02, //< Create an empty file for writing
     kAppendFl = 0x04, //< Open a file for writing at the end of the file.
     kUpdateFl = 0x08, //< Open a file for reading and writing.
     kBinaryFl = 0x10, //< Open a file for binary (not text) input/output.
     kStdoutFl = 0x20, //< Ignore fn use 'stdout'
     kStderrFl = 0x40, //< Ignore fn use 'stderr'
     kStdinFl  = 0x80, //< Ignore fn use 'stdin'
    };

    // Open or create a file.    
    // Equivalent to fopen().
    // If *hp was not initalized by an earlier call to fileOpen() then it should 
    // be set to fileNullHandle prior to calling this function. If *hp is a valid handle
    // then it is automatically finalized by an internal call to fileClose() prior to
    // being re-iniitalized.
    //
    // If kStdoutFl, kStderrFl or kStdinFl are set then 
    // file name argument 'fn' is ignored.
    rc_t open(    
      handle_t&    hRef,           // Pointer to a client supplied fileHandle_t to recieve the handle for the new object.
      const char* fn,             // The name of the file to open or create. 
      unsigned    flags );        // See fileOpenFlags_t

    // Close a file opened with  Equivalent to fclose().
    rc_t close(   handle_t& hRef );

    // Return true if the file handle is associated with an open file.
    bool       isValid( handle_t h );

    // Get the last error RC.
    rc_t  lastRC( handle_t h );

    // Read a block bytes from a file. Equivalent to fread().
    // 'actualByteCntRef is always the smae as bufByteCnt unless an error occurs or EOF is encountered.
    // This function checks lastRC() as a precondition and only proceeds if it is not set.
    rc_t read(    handle_t h, void* buf, unsigned bufByteCnt, unsigned* actualByteCntRef=nullptr );

    // Write a block of bytes to a file. Equivalent to fwrite().
    // This function checks lastRC() as a precondition and only proceeds if it is not set.
    rc_t write(   handle_t h, const void* buf, unsigned bufByteCnt );
  
    enum seekFlags_t 
    { 
     kBeginFl = 0x01, 
     kCurFl   = 0x02, 
     kEndFl   = 0x04 
    };

    // Set the file position indicator. Equivalent to fseek().
    rc_t seek(    handle_t h, enum seekFlags_t flags, int offsByteCnt );

    // Return the file position indicator. Equivalent to ftell().
    rc_t tell(    handle_t h, long* offsPtr );

    // Return true if the file position indicator is at the end of the file. 
    // Equivalent to feof().
    bool       eof(     handle_t h );

    // Return the length of the file in bytes
    unsigned   byteCount(  handle_t h );
    rc_t byteCountFn( const char* fn, unsigned* fileByteCntPtr );

    // Set *isEqualPtr=true if the two files are identical.
    rc_t compare( const char* fn0, const char* fn1, bool& isEqualFlRef );

    // Return the file name associated with a file handle.
    const char* name( handle_t h );

    // Write a buffer to a file.
    rc_t fnWrite( const char* fn, const void* buf, unsigned bufByteCnt );

    // Allocate and fill a buffer from the file.
    // Set *bufByteCntPtr to count of bytes read into the buffer.
    // 'bufByteCntPtr' is optional - set it to nullptr if it is not required by the caller.
    // It is the callers responsibility to delete the returned buffer with a
    // call to cmMemFree()
    char*  toBuf( handle_t h, unsigned* bufByteCntPtr ); 
  
    // Same as fileToBuf() but accepts a file name argument.
    char*  fnToBuf( const char* fn, unsigned* bufByteCntPtr );


    // Copy the file named in srcDir/srcFn/srcExt to a file named dstDir/dstFn/dstExt.
    // Note that srcExt/dstExt may be set to nullptr if the file extension is included
    // in srcFn/dstFn.  Likewise srcFn/dstFn may be set to nullptr if the file name
    // is included in srcDir/dstDir.
    rc_t    copy( 
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
    rc_t backup( const char* dir, const char* name, const char* ext, const char* dst_dir=nullptr );
  
    // Allocate and fill a zero terminated string from a file.
    // Set *bufByteCntPtr to count of bytes read into the buffer.=
    // (the buffer memory size is one byte larger to account for the terminating zero) 
    // 'bufByteCntPtr' is optional - set it to nullptr if it is not required by the caller.
    // It is the callers responsibility to delete the returned buffer with a
    // call to cmMemFree()
    char*  toStr( handle_t h, unsigned* bufByteCntPtr );

    // Same as fileToBuf() but accepts a file name argument.
    char*  fnToStr( const char* fn, unsigned* bufByteCntPtr );

    // Return the count of lines in a file.
    rc_t lineCount( handle_t h, unsigned* lineCntPtr );

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
    rc_t getLine( handle_t h, char* buf, unsigned* bufByteCntPtr );

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
    rc_t getLineAuto( handle_t h, char** bufPtrPtr, unsigned* bufByteCntPtr );

    // Binary Array Reading Functions
    // Each of these functions reads a block of binary data from a file and is a wrapper around file::read(h,buf,bufN).
    // The advantage to using these functions over file::read() is only that they are type specific.
    rc_t readChar(   handle_t h, char*           buf, unsigned cnt=1 );
    rc_t readUChar(  handle_t h, unsigned char*  buf, unsigned cnt=1 );
    rc_t readShort(  handle_t h, short*          buf, unsigned cnt=1 );
    rc_t readUShort( handle_t h, unsigned short* buf, unsigned cnt=1 );
    rc_t readLong(   handle_t h, long*           buf, unsigned cnt=1 );
    rc_t readULong(  handle_t h, unsigned long*  buf, unsigned cnt=1 );
    rc_t readInt(    handle_t h, int*            buf, unsigned cnt=1 );
    rc_t readUInt(   handle_t h, unsigned int*   buf, unsigned cnt=1 );
    rc_t readFloat(  handle_t h, float*          buf, unsigned cnt=1 );
    rc_t readDouble( handle_t h, double*         buf, unsigned cnt=1 );
    rc_t readBool(   handle_t h, bool*           buf, unsigned cnt=1 );

    inline rc_t read(   handle_t h, char&           x ){ return readChar(h,&x);   }
    inline rc_t read(  handle_t h, unsigned char&  x ){ return readUChar(h,&x);  }
    inline rc_t read(  handle_t h, short&          x ){ return readShort(h,&x);  }
    inline rc_t read( handle_t h, unsigned short& x ){ return readUShort(h,&x); }
    inline rc_t read(   handle_t h, long&           x ){ return readLong(h,&x);   }
    inline rc_t read(  handle_t h, unsigned long&  x ){ return readULong(h,&x);  }
    inline rc_t read(    handle_t h, int&            x ){ return readInt(h,&x);    }
    inline rc_t read(   handle_t h, unsigned int&   x ){ return readUInt(h,&x);   }
    inline rc_t read(  handle_t h, float&          x ){ return readFloat(h,&x);  }
    inline rc_t read( handle_t h, double&         x ){ return readDouble(h,&x); }
    inline rc_t read(   handle_t h, bool&           x ){ return readBool(h,&x);   }

    
    // Binary Array Writing Functions
    // Each of these functions writes an array to a binary file and is a wrapper around file::write(h,buf,bufN)
    // The advantage to using functions rather than fileWrite() is only that they are type specific.
    rc_t writeChar(   handle_t h, const char*           buf, unsigned cnt=1 );
    rc_t writeUChar(  handle_t h, const unsigned char*  buf, unsigned cnt=1 );
    rc_t writeShort(  handle_t h, const short*          buf, unsigned cnt=1 );
    rc_t writeUShort( handle_t h, const unsigned short* buf, unsigned cnt=1 );
    rc_t writeLong(   handle_t h, const long*           buf, unsigned cnt=1 );
    rc_t writeULong(  handle_t h, const unsigned long*  buf, unsigned cnt=1 );
    rc_t writeInt(    handle_t h, const int*            buf, unsigned cnt=1 );
    rc_t writeUInt(   handle_t h, const unsigned int*   buf, unsigned cnt=1 );
    rc_t writeFloat(  handle_t h, const float*          buf, unsigned cnt=1 );
    rc_t writeDouble( handle_t h, const double*         buf, unsigned cnt=1 );
    rc_t writeBool(   handle_t h, const bool*           buf, unsigned cnt=1 );

    inline rc_t write(   handle_t h, const char&           x ) { return writeChar(h,&x);   }
    inline rc_t write(  handle_t h, const unsigned char&  x ) { return writeUChar(h,&x);  }
    inline rc_t write(  handle_t h, const short&          x ) { return writeShort(h,&x);  }
    inline rc_t write( handle_t h, const unsigned short& x ) { return writeUShort(h,&x); }
    inline rc_t write(   handle_t h, const long&           x ) { return writeLong(h,&x);   }
    inline rc_t write(  handle_t h, const unsigned long&  x ) { return writeULong(h,&x);  }
    inline rc_t write(    handle_t h, const int&            x ) { return writeInt(h,&x);    }
    inline rc_t write(   handle_t h, const unsigned int&   x ) { return writeUInt(h,&x);   }
    inline rc_t write(  handle_t h, const float&          x ) { return writeFloat(h,&x);  }
    inline rc_t write( handle_t h, const double&         x ) { return writeDouble(h,&x); }
    inline rc_t write(   handle_t h, const bool&           x ) { return writeBool(h,&x);   }

    
    // Write a string to a file as <N> <char0> <char1> ... <char(N-1)>
    // where N is the count of characters in the string.
    rc_t writeStr( handle_t h, const char* s );

    // Read a string back from a file as written by fileWriteStr().
    // Note that the string will by string will be dynamically allocated
    // and threfore must eventually be released via mem::free().
    // If maxCharN is set to zero then the default maximum string
    // length is 16384.  Note that this limit is used to prevent
    // corrupt files from generating excessively long strings.
    rc_t readStr(  handle_t h, char** sRef, unsigned maxCharN );

    // Formatted Text Output Functions:
    // Print formatted text to a file.
    rc_t print(   handle_t h, const char* text );
    rc_t printf(  handle_t h, const char* fmt, ... );
    rc_t vPrintf( handle_t h, const char* fmt, va_list vl );
  
  }
  
}




#endif
