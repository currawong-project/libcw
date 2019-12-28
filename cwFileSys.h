#ifndef cwFileSys_H
#define cwFileSys_H

namespace cw
{
  namespace filesys
  {

    // Test the type of a file system object:
    //
    bool     isDir(  const char* dirStr ); //< Return true if 'dirStr' refers to an existing directory.
    bool     isFile( const char* fnStr );  //< Return true if 'fnStr' refers to an existing file.
    bool     isLink( const char* fnStr );  //< Return true if 'fnStr' refers to a symbolic link.
  
    // Create File Names:
    //
    // Create a file name by concatenating sub-strings.
    //
    // Variable arg's. entries are directories inserted between 
    // 'dirPrefixStr' and the file name.
    // Terminate var arg's directory list with a  nullptr. 
    //
    // The returned string must be released by a call to memRelease() or memFree().
    char* vMakeFn( const char* dir, const char* fn, const char* ext, va_list vl );
    char* makeFn(  const char* dir, const char* fn, const char* ext, ... );


    // Parse a path into its parts:
    //  
    // Return record used by pathParts()
    typedef struct
    {
      const char* dirStr;
      const char* fnStr;
      const char* extStr;
    } pathPart_t;

    // Given a file name decompose it into a directory string, file name string and file extension string.
    // The returned record and the strings it points to are contained in a single block of
    // memory which must be released by a call to memRelease() or memFree()
    pathPart_t* pathParts( const char* pathNameStr );
  }
  
}


#endif
