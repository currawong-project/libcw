
TODO: fix cwDsp.h: ampl_to_db(),db_to_ampl(), add pow_to_db() and db_to_pow().
Implement vectorized version in terms of the scalar versions in cwDsp.h.
Decide on standard dB range.  e.g. -100 to 0,  0 to 100 ....

* Flow Variables Class
** Attributes 
  + type: real,int,string,audio,spectrum,enum
  + flags: attribute flags
    - src: This variable must be connected.
    - multi: This variable may be instantiated multiple times
    - fan_in: This variable allows multiple incoming connections.
  + value:
    - real,int { min,max,value,center,step }
    - enum [ list of (id,label) pairs ]
    
  + doc: documentation string  

  + max_multi: max count of instantiations due to multiple connections
  + min_multi: min count of instantiations due to multiple connections

* Flow Proc Class
** Attributes
  + doc: documentation string
  + sub_proc:
    - sub_proc_cnt:<int>  set an absolute sub_proc_cnt
    - sub_proc_cnt_min:   
    - sub_proc_cnt_max:
      
    - sub_proc_var
      + label
      + flags: [ audio_chs, multi_chs, fan_in_chs ]

      Calculate the sub_proc_cnt based on the count of mult's,fan_ins, and audio channels.


    
* Var Map:

#+BEGIN_SRC c
  typedef struct variable_str
  {

    variable_str* var_link; // instance_t varL links 
  } variable_t;

  typedef struct proc_desc
  {
    var_desc_t* varDescA; // description of each base variable
    unsigned    varDescN; 

  } proc_desc_t;

  typedef struct varBase_str
  {
    char*       label;      // label assigned to this 'mult'
    unsigned    multIdx;    // mult index 
    variable_t* baseVar;    // all variables have a base instance (chIdx=kAnyChIdx)
    unsigned    subProcN;   // count of times this variable is replicated to specialize for a given subprocess
    variable_t* subProcA[ subProcN ]; // 
  } varBase_t;

  typedef struct varMap_str
  {
    unsigned   multN;  // count of times this variable is replicated based on multiple incoming connections to the same input variable label.
    varBase_t* multA[ multN ] // pointer to each base variable
  } varMap_t;

  typedef struct instance_str
  {
    variable_t* varL;  // variable linked list: list of all variable instances
    unsigned maxVId; // maximum application supplied vid. In general maxVId+1 == proc_desc_t.varDescN
    varMap_t varMap[ maxVId ]; // maps an application vid to a list of variable instances
  } instance_t;

  

#+END_SRC


  
* 
* Plan



** Flow processor 'multi' processor:
   Add the ability for a processor to expand the number of variables based on
   incoming connections. 
   - Variables with this capability must have the 'multi' attribute in the class description.
   - The new variables will be named by adding a suffix in the connection command.
     e.g. in:{ in.a:out.foo } connect the output out.foo to a new variable instantiated
     on the the local variable description 'in'.
   - The new variable may then be addressed by referring to 'in.a'.
   - The proc instance may then ask for a count of variable instances for a given base varaible.
     var_get_multi_count( ...,'in') and address them by var_get( ...,'in',multi_idx).
   - Note that once a variable is given the 'multi' attribute the only way for the instance
     to access the variable is by supplying the 'multi' index since the variable
     label alone will not be adequate to select among multiple instances.
   
     
** Flow processor Fan-in capability:
    Add the ability for a processor variables to support multiple incoming connections.
   
   - Fan-in capability must be enabled in the processor class description with the 'fan-in' attribute.
   - The array of variables associated with fan-in connections will be
     addressed via "<label>.<integer>".
   - The count of inputs to a fan-in varaible instance can be accessed via: var_fan_in_count( ...,var_label)
   - The variable instance associated with each fan-in connection can be accessed with
     var_get( ...,'in',fan_in_idx).
   - Note that once a variable is given the 'fan-in' attribute a fan_in_idx must be used to access it.

    

     

** Add MIDI processors - this may be complicated by cross fading scheme.
   - maybe cross-faded proc's should be all placed in a 'sub-net' and
   only those processes would then be cross faded.

   
** Add subnets. (see outline below)
** Add auto-UI (this will require a separate app).


* Functionality
** libcw:

- Remove dependency on locally built websockets library.

- Remove applications from the libcw folder and put them in their
own folders. (breakout libcw from application / reorganize project)
Allow ui.js to be shared by all apps.


** UI:
- Add support for custom controls
- Document the UI resource file format.
- Document the UI client/server protocol.
1. The message formats to and from the server and the javascript client.
2. When the messages are sent.

- UI: Add an option to print the UI elment information as they are created.
This is a useful way to see if id maps are working.
Print: ele name, uuid, appId and parent name, uuid, appId


** Flow:

- Create automatic UI for proc's.
- Create the ability to script sub-networks.
- Create true plug-in architecture - requires a C only interface.
- Add a callback function to inform the app when a variable changes.
  The same callback could be used to map variable labels to id's at startup.
  This callback may be a key part of adding an automatic UI.
- Simplify the coding of processors by having the system
  call the instance for each variable.  This will make createing most
  processors a matter of responding to simple requests from the system.
  More complex processors could still be created using the current technique
  of calling explicit functions (e.g. `register_and_get(), register_and_set()`)
  

*** Subnet scheme:
```
{
    balanced_mix: {

	doc: "This is a two channel balancer network.",

    network: {
		ain:    { class: port, source:merge.in0, doc:"Audio input."},
		ain_alt:{ class: port, source.merge.in1, doc:"Alternate audio input."},
		bal_in  { class: port, type: real,       doc:"Mix balance control." },
				    
		bal:    { class: balance,     in:{ in:bal_in.out } },	    
		merge:  { class: audio_merge, in:{ in.0:ain, in.1:ain_alt } }
		gain:   { class: audio_gain   in:{ in:merge.out, gain:bal.out } },
	    
		aout:   { class: port, type: audio, in:{ gain.out } }
	  }
    } 
}
```
- Create a class description by parsing the subnet and converting 
the 'port' instances into a variable definitions.

- Port instances are just proc's that have a single variable but do not implement
any of the processing functions.  The variables act as 'pass-through' variables
that connect variables outside the subnet to variables inside the subnet.

- The subnet itself will be held inside an 'instance_t' and will pass
on 'exec()' calls to the internal instance processor chain.

- The current flow architecture only allows static connections.
This allows proc variables to be proxied to other proc variables.
This doesn't scale well for processes with many variables (e.g. matrix mixer).
For processes with many variables a message passing scheme works better
because it allows a message to dynamically address a process
(e.g. (set-in-channel-1-gain-to-42) 'set','in',1,'gain',42), 'set','out',4,'delay',15)

Note that it would be easy to form these messages on the stack and 
transmit them to connected processes. 

* To do list:

** libcw
- Fix the time functions to make them more convenient and C++ish.
- libcw: document basic functionality: flow, UI, Audio

** Flow

- Implement MIDI processors.
- Implement flow procs for all libcm processsors.
- Create default system wide sample rate.
- Allow gain to be set on any audio input or output.
- flow metering object with resetable clip indicator and audio I/O meters
- indicate drop-outs that are detected from the audio IO system
- allow a pre/post network before and after cross fader
- allow modifiable FFT window and hop length as part of preset 
- add selectable audio output file object to easily test for out of time problems

- Add attributes to proc variables:
  1. 'init' this variable is only used to initialize the proc. It cannot be changed during runtime.  (e.g. audio_split.map)
  2. 'scalar' this variable may only be a scalar.  It can never be placed in a list.  (e.g. sine_tone.chCnt)
  3. 'multi' this src variable can be repeated and it's label is always suffixed with an integer. 
  4. 'src' this variable must be connected to a source.
  5. 'min','max' for numeric variables.
  6. 'channelize' The proc should instantiate one internal process for each input channel. (e.g. spec_dist )
  
- Create a var args version of 'var_get()' in cwFlowTypes.h.

- add attribute list to instances: [ init_only, scalar_only, print="print values", ui ]
- why are multiple records given in the 'args:{}' attribute?


** UI:

- Notes on UI id's:
1. The appId, when set via an enum, is primarily for identifying a UI element in a callback switch statement.
There is no requirement that they be unique - although it may be useful that they are guaranteed to be unique
or warned when they are not. Their primary use is to identify a UI element or class of UI
elements in a callback switch statement. Note that the callback also contains the uuId of the element
which can then be used to send information back, or change the state of, the specific element which 
generated the callback. In this case there is never a need to do a appId->uuId lookup because the
callback contains both items.  

2. UUid's are the preferred way to interact from the app to the UI because they are unique
and the fastest way to lookup the object that represents the element.

3. The 'blob' is useful for storing application information that is associated with an UI element.
Using the 'blob' can avoid having to maintain a data structure which parallels the internal
UI data structure for application related data.  The 'blob' can be accessed efficiently via the uuId.

4. The most labor intensive GUI related accesses are changing the state of a UI element outside
of a callback from that GUI element.  In this case it may be advantageous to store UUID's of elements
which affect one anothers state within each others blobs.  Alternatively use 
uiElementChildCout() and uiElementChildIndexToUuid() or uiElementChildAppIdToUuid() to 
iterate child elements given a parent element uuid.


- Fix crash when '=' is used as a pair separator rather than ':'.
cwUi is not noticing when a UI resource file fails to parse correctly.
This may be a problem in cwObject or in cwUI.

- Fix bug where leaving out the ending bracket for the first 'row' div in ui.cfg
causes the next row to be included in the first row, and no error to be generated,
even though the resource object is invalid (i.e. there is a missing brace).

- The UI needs to be better documented. Start by giving clear names
to the various parts: Browser, UI Manager, UI Server, Application.
Maybe describe in Model,View,Controller terms?

- Document the meaning and way that id's and names/labels are used,
and intended to be used, and found by UI. As it is they are confusing.

- The UI app id map should be validated after the UI is created.
In otherwords the parent/child pairs shoud actually exists.

- Arrange the project layout so that all the UI based apps use the same ui.js.
Currently changes and improvements to one version of ui.js cannot be automatically
shared.

- uiSetValue() should be optionally reflected back to the app with kValueOpId messages.
This way all value change messages could be handled from one place no matter
if the value changes originate on the GUI or from the app.

- The ui manageer should buffer the current valid value of a given control
so that the value can be accessed synchronously. This would prevent the application
from having to explicitely store all UI values and handle all the 'value' and 'echo'
request.  It would support a model where the UI values get changed and then
read by the app (e.g. getUiValue( appId, valueRef)) just prior to being used.
As it is the UI values that are on the interface cannot be accessed synchronously
instead the app is forced to notice all 'value' changes and store the last legal value.
(12/22: Given that the cwUi.cpp _transmitTree() function appears to the current
value of each control to new remote WS Sessions - the value may actually already
be available.  Examine how this works.  Is 'value' and 'attribute' like 'order'?)

- Using the 'blob' functionality should be the default way for tying UI elements to program model.
  Rewrite the UI test case to reflect this.

- Add an ui::appIdToUuId() that returns the first matching appId, and then
optionally looks for duplicates as an error checking scheme. 


- The ui eleA[] data structure should be changed to a tree 
because the current expandable array allows empty slots which need to be checked
for whenever the list is iterated.  It is also very inefficient to delete from the
eleA[] because an exhaustive search is required to find all the children of the
element to be deleted.

- UI needs a special UUID (not kInvalidId) to specify the 'root' UI element. See note in cwUi._createFromObj()


** Audio:


- Should a warning be issued by audioBuf functions which return a set of values:
muteFlags(),toneFlags(), gain( ... gainA) but where the size of the dest array
does not match the actual number of channesl?

- cwAudioBuf.cpp - the ch->fn in update() does not have the correct memory fence.

- Replace 24 bit read/write in cwAudioFile.cpp

- Remove Audio file operations that have been superceded by 'flow' framework.


** Socket
- Any socket function which takes a IP/port address should have a version which also takes a sockaddr_in*.


** Websocket

- cwWebsock is allocating memory on send().
- cwWebsock: if the size of the recv and xmt buffer, as passed form the protocolArray[], is too small send() will fail without an error message.
This is easy to reproduce by simply decreasing the size of the buffers in the protocol array.

## Object
- Look at 'BUG' warnings in cwNumericConvert.h.
- cwObject must be able to parse without dynamic memory allocation into a fixed buffer
- cwObject must be able to be composed without dynamic memory allocation or from a fixed buffer.


- Clean up the cwObject namespace - add an 'object' namespace inside 'cw'

- Add underscore to the member variables of object_t.

- numeric_convert() in cwNumericConvert.h could be made more efficient using type_traits.

- numeric_convert() d_min is NOT zero, it's smallest positive number, this fails when src == 0.
  min value is now set to zero.

- Change file names to match object names

- Improve performance of load parser. Try parsing a big JSON file and notice how poorly it performs.


** Misc
- logDefaultFormatter() in cwLog.cpp uses stack allocated memory in a way that could easily be exploited.

- lexIntMatcher() in cwLex.cpp doesn't handle 'e' notation correctly. See note in code.

- thread needs setters and getters for internal variables

- change cwMpScNbQueue so that it does not require 'new'.

- (DONE) change all NULL's to nullptr
- (DONE) implement kTcpFl in cwTcpSocket.cpp

** Documentation

*** UI Control Creation Protocol

The UI elements have four identifiers:

uuId  - An integer which is unique among all identifiers for a given cwUi object.
appId - A constant (enumerated) id assigned by the application. Unique among siblings.
jsId  - A string id used by Javascript to identify a control. Unique among siblings.
jsUuId - An integer which is unique among all identifers for the browser representation of a given cwUi object.

The 'jsId' is selected by the application when the object is created.
The 'jsUuId' is generated by the JS client when the UI element is created.
The 'uuId' is generated by the UI server when the JS client registers the control.
The 'appId' is assigned by the UI server when the JS client regsiters the control.

Client sends 'init' message.
Server sends 'create' messages.
Client sends 'register' messages.
Server send' 'id_assign' messages.

*** sockaddr_in reference


    #include <netinet/in.h>

    struct sockaddr_in {
        short            sin_family;   // e.g. AF_INET
        unsigned short   sin_port;     // e.g. htons(3490)
        struct in_addr   sin_addr;     // see struct in_addr, below
        char             sin_zero[8];  // zero this if you want to
    };

struct in_addr {
    unsigned long s_addr;  // load with inet_aton()
};



*** Development Setup

0)
```
  sudo dnf install g++ fftw-devel alsa-lib-devel libubsan
```
1) Install libwebsockets.

```
    sudo dnf install openssl-devel cmake
    cd sdk
    git clone https://libwebsockets.org/repo/libwebsockets
    cd libwebsockets
    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX:PATH=/home/kevin/sdk/libwebsockets/build/out ..
```

2) Environment setup:

    export LD_LIBRARY_PATH=~/sdk/libwebsockets/build/out/lib

*** Raspberry Pi Build Notes:

    cd sdk
    mkdir libwebsockets
    cmake -DCMAKE_INSTALL_PREFIX:PATH=/home/pi/sdk/libwebsockets/build/out -DLWS_WITH_SSL=OFF ..
    make
    sudo make install

    apt install libasound2-dev


    
*** Flow Notes:


- When a variable has a variant with a numberic channel should the 'all' channel variant be removed?

- Check for duplicate 'vid'-'chIdx' pairs in var_regster().
  (The concatenation of 'vid' and 'chIdx' should be unique 


- When a proc. goes into exec state there should be a guarantee that all registered variables
can be successfully read. No error checking should be required.

(How about source variables? these can never be written.)

- Make an example of a repeating input port.  For example a mixer than takes
audio input from multiple processors.

- Make an example of a proc that has a generic port which allows any type, or a collection of
specific types, to pass through. For example a 'selector' (n inputs, 1 output) or a router
(1 signal to n outputs)

- Create a master cross-fader.

DONE:  Add a version of var_register() that both registers and returns the value of the variable.

*** Flow Instance Creation:

1. Create all vars from the class description and initially set their
value to the default value given in the class.  chIdx=kAnyChIdx.

Note that all vars must be included in the class description.


2. Apply the preset record from the class description according to the
label given in the instance definition. 

If the variable values are given as a scalar then the existing
variable is simply given a new value.

If the variable values are given as a list then new variables 
records will be created with explicit channels based on the
index of the value in the list. This is referred
to as 'channelizing' the variable because the variable will then
be represented by multiple physical variable records - one for each channel.
This means that all variables will have their initial record, with the chIdx set to 'any',
and then they may also have further variable records will for each explicit
channel number. The complete list of channelized variable record 
is kept, in channel order, using the 'ch_link' links with the base of the list
on the 'any' record.

3. Apply the variable values defined in the instance 'args' record.
This application is treated similarly to the 'class' 
preset. If the variable value is presented in a list then
the value is assigned to a specific channel if the channel
already exists then the value is simply replaced, if the
channel does not exist then the variable is 'channelized'.

4. The varaibles listed in the 'in' list of the instance cfg.
are connected to their source variables.

5. The custom class constructor is run for the instance.

Within the custom class constructor the variables to be used by the
instance are 'registered' via var_register().  Registering
a variable allows the variable to be assigned a constant
id with which the instance can access the variable very efficiently.

If the channel associated with the registered variable does not
yet exist for the variable then a channelized variable is first created
before registering the variable.

6. The internal variable id map is created to implement  fast
access to registered variables.





