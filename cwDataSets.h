#ifndef cwDataSets_h
#define cwDataSets_h
/*

Select a dataset and provide batched data/label pairs.

1. In-memory datasets, stream from disk.
2. Train/valid/test set marking.
3. K-fold rotation.
2. Conversion from source data type to batch data type.
3. One-hot encoding.
4. Shuffling.

Options:
  1. Read all data into memory (otherwise stream from disk -require async reading)
  2. data type conversion on-load vs on-batch.
  3. one-hot encoding on-load vs on-batch.
  4. shuffle 
       a. from streaming input buffer.
       b. in memory
       c. on batch


Source Driver:
  label()       // string label of this source
  open(cfg)     // open the source
  close()       // close the source
  get_info()    // get the source dim and type info
  read(N,dst_t,dataBuf,labelBuf);// read a block of N examples and cvt to type dst_t

Implementation:
  The only difference between streaming from disk and initial load to memory is that 
stream-from-disk fills a second copy of the in-memory data structure.

All set marking, both RVT and K-Fold, happen on the in-memory data structure after it is populated.

Shuffling happens on the in-memory data structure after it is populated.
If there is no data conversion or one-hot conversion on batch output then shuffling moves elements in-memory otherwise
the shuffle index vector is used as a lookup during the output step.

If K-Fold segmentation is used with a streaming dataset then the k-fold index must persist
between fold selection passes.

 */


namespace cw
{
  namespace dataset
  {
    
    /*

       wtr: Writes columnar numeric files one row at a time. The data in a column
       may be multidimensional. In othe words the data in a column may be a matrix.
       Furthermore the data in a column may have a variable shape.
       
       Usage:
       1. Use define_columns() to name and describe the shape of the data in each column.
          If a data has a variable size then set the variable dimension to 0.
       2. For each row in the source dataset
       3.    For each column in the source dataset
       4.       Call wtr::write() to cache the column contents
       5.    Call write_record() to write the record to disk.

       Notes:
       a. The data type of a column is determined by the data type of the column in the first row.
       b. The data type of a column may not change after the first row.
       
       
       
      File Format:
      Offset | Field | Label
      -------|-------|------------------------
          4  |     0 | record_count
          4  |     1 | column_count

          v  |  0  2 | label [ cnt, c0, c1, c2 ...]
          4  |  1  3 | id 
          4  |  2  4 | varDimN
          4  |  3  5 | rankN
          4  |  4  6 | maxEleN
          4  |  5  7 | max typeflags
          v  |  6  8 | max value 
          4  |  7  9 | min typeflags
          4  |  8 10 | min value
          4  |  9 11 | dimV[0]
          4  | 10 12 | maxDimV[0] 
          4  |     . | dimV[1]
          4  |     . | maxDimV[1] 
          
                                       
                                        column 0                column 1                 column N
                                      ---------------------- ---------------------      ---------------------
       Row Format: { <row_byte_count> { <varDimV0> <data0> } { <varDimV1> <data1> } ... { <varDimVN> <dataN> } }
             
       Note that if a column's data has a fixed size then the <varDimV> is empty.

     */
    
    namespace wtr
    {      
      typedef handle<struct wtr_str> handle_t;
      
      rc_t create(  handle_t& h, const char* fn );
      rc_t destroy( handle_t& h );

      // Define the shape of each column. Set variable length dimensions to 0.
      rc_t define_columns( handle_t h, const char* label, unsigned columnId, unsigned rankN, const unsigned* dimV );

      // Cache one column of data which will then be written on the call to write_record().
      // If all the dimensions are defined in the column configuration then set dimV to nullptr;
      rc_t write( handle_t h, unsigned columnId, const int*    dV, unsigned dN, const unsigned* dimV=nullptr, unsigned dimN=0 );
      rc_t write( handle_t h, unsigned columnId, const float*  dV, unsigned dN, const unsigned* dimV=nullptr, unsigned dimN=0 );
      rc_t write( handle_t h, unsigned columnId, const double* dV, unsigned dN, const unsigned* dimV=nullptr, unsigned dimN=0 );

      // Write the 
      rc_t write_record( handle_t h );

      rc_t test( const object_t* cfg );
            
    }

    namespace rdr
    {
      typedef handle<struct rdr_str> handle_t;

      enum
      {
        kIntRdrFl    = 0x01,
        kFloatRdrFl  = 0x02,
        kDoubleRdrFl = 0x04
      };
              
      typedef struct col_str
      {
        const char*      label;      // Unique column label
        unsigned         id;         // Unique column id
        unsigned         typeId;     // See k???RdrFl type flags
        unsigned         varDimN;    // Count of variable sized dimensions. 0 if this is a fixed size column.
        unsigned         rankN;      // Count of elements in dimV[] 
        unsigned*        dimV;       // dimV[rankN].  Dimensions with value zero are undefined and set per field.
        unsigned         eleN;       // Size of current column value
        unsigned*        maxDimV;    // maxDimV[rankN]. Maximum value for each dimension. Same as dimV[]
        
        variant::value_t max;        // Max value of all data elements in this field
        variant::value_t min;        // Min value of all data elements in this field

        unsigned         maxEleN;    // Max. count of elements in any one field.
        unsigned         maxByteN;   // Max. size of this field in bytes
                
        unsigned         byteOffset; // Byte offset of the value of this field in the current record buffer.
        unsigned         byteN;      // Size of this field in bytes.
      } col_t;
      
      rc_t create(  handle_t& h, const char* fn );
      rc_t destroy( handle_t& h );

      unsigned     column_count( handle_t h );
      const col_t* column_cfg( handle_t h, unsigned colIdx );
      const col_t* column_cfg( handle_t h, const char* colLabel );
      
      unsigned     record_count( handle_t h);
      
      unsigned     cur_record_index(  handle_t h );
      unsigned     next_record_index( handle_t h );

      enum {
        kOkState,     // Normal state
        kErrorState,  // An error has occurred which render the rdr unusable.
        kEofState     // The end of the file has been encountered.
      };
      
      unsigned state( handle_t h );

      rc_t seek( handle_t h, unsigned recordIdx );
      
      // Read the next record.
      rc_t read( handle_t h, unsigned recordIdx=kInvalidIdx );

      // Read a column value.
      //
      // vRef = Pointer to the value vector.
      // nRef = Count of elements in value vector.
      // dimVRef = Dimension vector.  nRef = cumprod(dimVRef) 
      rc_t get( handle_t h, unsigned columnId, const int*&    vRef, unsigned& nRef, const unsigned*& dimVRef );
      rc_t get( handle_t h, unsigned columnId, const float*&  vRef, unsigned& nRef, const unsigned*& dimVRef );
      rc_t get( handle_t h, unsigned columnId, const double*& vRef, unsigned& nRef, const unsigned*& dimVRef );
      
      rc_t report( handle_t h );

      rc_t test( const object_t* cfg );
    }

    namespace adapter
    {
      typedef handle<struct adapter_str> handle_t;

      enum {
        kPreInitState,
        kInitState,
        kEofState,
        kErrorState
      };
      
      enum {

        kTrackColDimFl = 0x01,
        
        kIntFl    = 0x10,  // Field Type Flags: int
        kFloatFl  = 0x20,  //                   float
        kDoubleFl = 0x40,  //                   double
        kTypeMask = 0x70   //                   (int | float | double)
      };

      typedef struct colMap_str
      {
        unsigned        colId;           // Column identifier from the rdr
        unsigned        fieldEleOffset;  // Offset into field record of this column
        unsigned        eleN;            // Count of elements in this column
        const unsigned* dimV;            // Shape of this column
        unsigned        rankN;           // dimV[ rankN ] Rank of this column
      } colMap_t;

      
      rc_t create(  handle_t& hRef, const char* fn, unsigned maxBatchN );
      rc_t destroy( handle_t& hRef );

      // Create a field and assign it a column.
      rc_t create_field(  handle_t h, unsigned fieldId, unsigned flags, const char* colLabel=nullptr, bool oneHotFl=false );

      // Assign an additional column to a field
      rc_t assign_column( handle_t h, unsigned fieldId, const char* colLabel, bool oneHotFl=false );

      // Total count of records in the dataset.
      unsigned record_count( handle_t h );
      
      // Field element count for fixed size fields.
      unsigned field_fixed_ele_count( handle_t h, unsigned fieldId );

      // Read and cache batchN records.
      // recordIdxV[ batchN ] is an optional array of record indexes
      rc_t read( handle_t h, unsigned batchN, const unsigned* recordIdxV=nullptr );

      // Return field vectors formed on the previous call to read().
      // fV[ eleN, batchN ]
      // fNV[ batchN ] = eleN for each column of vV[]
      rc_t get( handle_t h, unsigned fieldId, const int*&    fV_Ref, const unsigned*& fNV_Ref ); 
      rc_t get( handle_t h, unsigned fieldId, const float*&  fV_Ref, const unsigned*& fNV_Ref ); 
      rc_t get( handle_t h, unsigned fieldId, const double*& fV_Ref, const unsigned*& fNV_Ref );

      // Returns col position and geometry data from each record returned by the last
      // call to read().
      // Returns colMapV_Ref[batchN][columnN].
      rc_t column_map( handle_t h, unsigned fieldId, colMap_t const * const *& colMapV_Ref );

      // See k???State above for return values.
      unsigned state( handle_t h );

      // Print a field to stdout. If fmt==nullptr then a format is automatically set based on the data type.
      rc_t print_field( handle_t h, unsigned fieldId, const char* fmt=nullptr );
      
      rc_t test( const object_t* cfg );
      
    }

    
    namespace mnist
    {
      typedef handle<struct mnist_str> handle_t;

      rc_t create( handle_t& h, const char* inDir );
      rc_t destroy( handle_t& h );
      
      unsigned record_count( handle_t h );
      
      rc_t     seek(   handle_t h, unsigned exampleIdx );
      rc_t     dataM(  handle_t h, const float*& dataM,  const unsigned*& labelV, unsigned exampleN, unsigned& actualExampleN_Ref, unsigned exampleIdx=kInvalidIdx );

      rc_t    write( handle_t h, const char* fn );

      rc_t test( const object_t* cfg );
    }

    
    rc_t test( const object_t* cfg );
   
  }

  
}


#endif
