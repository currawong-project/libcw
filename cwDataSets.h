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
    namespace mnist
    {
      typedef handle<struct mnist_str> handle_t;

      rc_t create( handle_t& h, const char* dir );
      rc_t destroy( handle_t& h );
      
      // Each column has one example image.
      // The top row contains the example label.
      const mtx::f_t* train(    handle_t h );
      const mtx::f_t* validate( handle_t h );
      const mtx::f_t* test(     handle_t h );

      rc_t test(const char* dir, const char* imageFn );
    }



      

    typedef handle<struct datasetMgr_str> handle_t;

    // Data subset flags
    enum { kTrainSsFl=0x10, kValidSsFl=0x20, kTestSsFl=0x40 };
    

    enum { kFloatFl=0x02, kDoubleFl=0x04 };
    rc_t create( handle_t& h, const object_t* cfg, unsigned flags );
    rc_t destroy( handle_t& h );


    // Load a dataset, divide it into train,validate, and test subsets
    rc_t load( handle_t h, const char* dsLabel, unsigned batchN, unsigned validPct, unsigned testPct, unsigned flags );

    // Shuffle the subset.
    rc_t shuffle( handle_t h, unsigned subsetFl );
    
    // Get the dimensions of all the examples from a subset.
    // dimN=1:  dimV[0]=batchN
    // dimN=2:  dimV[0]=realN   dimV[1]=batchN
    // dimN=3:  dimV[0,1]=realN dimV[2]=batchN
    rc_t subset_dims( handle_t h, unsigned subsetFl, const unsigned*& dimV_Ref, unsigned& dimN_Ref );
    rc_t label_dims(  handle_t h, unsigned subsetFl, const unsigned*& dimV_Ref, unsigned& dimN_Ref );


    // get the next batch. Returns nullptr at the end of an epoch.
    rc_t batch_f(  handle_t h, unsigned subsetFl, const float*&  dataM_Ref,  const float*& labelM_Ref );
    rc_t batch_d(  handle_t h, unsigned subsetFl, const double*& dataM_Ref, const double*& labelM_Ref );

    rc_t test( const object_t* cfg );
    
  }

  
}


#endif
