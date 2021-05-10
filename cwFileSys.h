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
    // The returned string must be released by a call to mem::release() or mem::free().
    char* vMakeFn( const char* dir, const char* fn, const char* ext, va_list vl );
    char* makeFn(  const char* dir, const char* fn, const char* ext, ... );

    char* vMakeVersionedFn(const char* dir, const char* fn_prefix, const char* ext, va_list vl );
    char* makeVersionedFn( const char* dir, const char* fn_prefix, const char* ext, ... );

    // Replace the directory/name/extension part of a complete path.
    // The returned string must be release by a call to mem::release() or mem::free().
    char* replaceDirectory( const char* fn, const char* dir  );
    char* replaceFilename(  const char* fn, const char* name );
    char* replaceExtension( const char* fn, const char* ext  );


    // The returned string must be released by a call to mem::release() or mem::free().
    char* expandPath( const char* dir );


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
    // memory which must be released by a call to mem::release() or mem::free()
    pathPart_t* pathParts( const char* pathNameStr );

    // Flags used by dirEntries 'includeFlags' parameter.
    enum
    {
     kFileFsFl         = 0x001,   //< include all visible files
     kDirFsFl          = 0x002,   //< include all visible directory 
     kLinkFsFl         = 0x004,   //< include all symbolic links
     kInvisibleFsFl    = 0x008,   //< include file/dir name beginning with a '.'
     kCurDirFsFl       = 0x010,   //< include '.' directory
     kParentDirFsFl    = 0x020,   //< include '..' directory

     kAllFsFl          = 0x02f,   //< all type flags

     kFullPathFsFl     = 0x040,   //< return the full path in the 'name' field of dirEntry_t;
     kRecurseFsFl      = 0x080,   //< recurse into directories
     kRecurseLinksFsFl = 0x100    //< recurse into symbol link directories 
    };

    // The return type for dirEntries().
    typedef struct
    {
      unsigned    flags;    //< Entry type flags from kXXXFsFl.
      const char* name;     //< Entry name or full path depending on kFullPathFsFl.
    } dirEntry_t;

    // Return the file and directory names contained in a given subdirectory.
    //
    // Set 'includeFlags' with the  kXXXFsFl flags of the files to include in the returned 
    // directory entry array.  The value pointed to by dirEntryCntPtr will be set to the
    // number of records in the returned array.
    dirEntry_t* dirEntries( const char* dirStr, unsigned includeFlags, unsigned* dirEntryCntRef );



    rc_t makeDir( const char* dirStr );
  }
  
}


#endif
