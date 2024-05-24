Audio Dev. File
1. Try different combinations of including input and output channels and groups.
   Specify an input file, but not an input group. Specify an input group but not an input file ....


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

### Raspberry Pi Build Notes:

    cd sdk
    mkdir libwebsockets
    cmake -DCMAKE_INSTALL_PREFIX:PATH=/home/pi/sdk/libwebsockets/build/out -DLWS_WITH_SSL=OFF ..
    make
    sudo make install

    apt install libasound2-dev


    
### Flow Notes:


- When a variable has a variant with a numeric channel should the 'all' channel variant be removed?

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

### Flow Instance Creation:

0. Parse the 'in' list and create any 'mult' variables whose 
'in-var' contains an integer or underscore suffix. See
"'in' List Syntax and Semantics" below.

1. Create all vars from the class description, that were not
already instantiated during 'in' list processing, and set their
initial value to the default value given in the class.  chIdx=kAnyChIdx.

Note that all vars must be included in the class description.


2. Apply the preset records from the class description according to the
'presets' list given in the instance definition. 

If the variable values are given as a scalar then the existing
variable is simply given a new value.

If the variable values are given as a list then new variables 
records will be created with explicit channels based on the
index of the value in the list. This is referred
to as 'channelizing' the variable because the variable will then
be represented by multiple physical variable records - one for each channel.
This means that all variables will have their initial record, with the chIdx set to 'any',
and then they may also have further variable records for each explicit
channel number. The complete list of channelized variable record 
is kept, in channel order, using the 'ch_link' links with the base of the list
on the 'any' record.

3. Apply the variable values defined in a instance 'args' record.
The 'args' record may have multiple sets of args. 
If the preset instance includes an 'argsLabel' value then this record
is selected to be applied.  If No 'argsLabel' is given then
the record named 'default' is selected.  If there is no record
named 'default' then no record is applied.

The application of the args record proceeds exactly the same as
applying a 'class' preset. If the variable value is presented in a
list then the value is assigned to a specific channel. If the channel
already exists then the value is simply replaced. If the channel does
not exist then the variable is 'channelized'.

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

7. The 

# Notes on 'poly' and 'mult':

The 'in' statement is formed by a list of _Connect Expressions_ :

`<input_var>:<source_inst.source_var>`

There are three forms of connect expressions:

1. Simple Connect Expression: Both the input and source labels
identify vars in the input and source instance.

2. Manual Mult Connect Expression: The input identifer ends with an
integer.  This expression indicates that an input var will be
instantiated and connected to the source var.  The integer indicates
the suffix (sfx) id of the input var. e.g. `in0:osc.out`,`in1:filt.out`.
      
3. PolyMult Connect Expression: The source identifier has an
underscore suffix.  This form indicates that there will one instance of
this var for each poly instance that the source var instances is
contained by. e.g. `in:osc_.out` If `osc` is contained by an order 3
poly then statement will create and connect three instances of `in` -
`in0:osc0.out`,`in1:osc1.out` and `in2:osc2.out`.

Notes:
- For an input variable to be used in either of the 'Manual' or 'PolyMult' 
forms the var class desc must have the 'mult' attribute.

- If any var has an integer suffix then this is converted to it's sfx id.

- If the input var of a poly mult expression has an integer suffix then this is taken to be the
base sfx id for that poly connection.  Other connections in the same statement will be
incremented from that base value.  e.g `in3:osc_.out` becomes
`in3:osc0.out`,`in4:osc1.out` and `in5:osc2.out`.

- The first and last poly source instance can be indicated by specifying a 
begin poly index and count before and after the source index underscore:
e.g. `in:osc3_3.out` becomes: `in0:osc3.out`,`in1:osc4.out` and `in2:osc5.out`.

- A similar scheme can be used to indicate particular source instance vars:
`in:osc.out1_2` becomes `in0:osc.out1`,`in1:osc.out2`

- It is a compile time error to have more than one input variable with the same sfx id.

'in' List Syntax and Semantics:
===============================

Syntax:
-------
The 'in' list has the follow syntax:
`in: { in-stmt* }`
`in-stmt`     -> `in_expr`":" `src_expr`
`in-expr`     -> `in-proc-id`".`in-var-id`
`src-expr`    -> `src-proc-id`"."`src-var-id`
`in-var-id`   -> `var-id`
`src-proc-id` -> `var-id`
`src-var-id`  -> `var-id`
`var-id`      -> `label` { `label-sfx` }
`label-sfx`   -> { `pri-int`} {{"_"} `sec-int` }
`pri-int`     -> int
`sec-int`     -> int


Semantics:
----------

### `in-proc-id`

- The `in-proc-id` is only used when the in-stmt
is iterating over the in-proc sfx-id. 
This precludes iterating over the in-var, as discussed below.

In this case the only useful option is to set the 'var-id` to `_`
as the in-proc is taken as the the proc which the
in-stmt belongs to.

The iterating source and/or var sfx-id are then set
to the current proc sfx-id + source `pri-int`.


### `in-var-id`

- The `label` part of the `in-var-id`  must match to a 
var description in the input proc class description.

- If no `label-sfx` is given then no special action
need by taken at var creation time. This var will be
created by default and later connected to the source inst/var.


- (0) If the "_" is given:
    + This is an "iterating" in-stmt where multiple 
	  input vars will be created and connected.
	
	+ If no `pri-int` is given  then the `pri-int` defaults to 0.
	
	+ If the `pri-int` is given then it indicates that 
      an instance of this var should be created where
      the `pri-int` becomes the sfx-id of the var instance.

	+ If `sec-int` is given then it gives the 
      count of input vars which will be created. The
	  sfx-id of each new input var begins with `pri-int` 
	  and increments for each new var.

    + (1) If no `sec-int` is given then the `sec-int` is implied by the count
	  of source variables indicated in the `src-expr`.


- If "_" is not given:
    + No `sec-int` can exist without a "_".
	
    + If a `pri-int` is given then a single
	  input var is created and the `pri-int` 
	  gives the sfx-id.  This single input
	  var is then connected to a single src var.
	  
	+ If no `pri-int` is given
	  then the default var is created
	  with kBaseSfxId and is connected
      to a single source var.


### `src-proc-id`

- The `label` part of the `src-proc-id` must match to a 
previously created proc instance in the current network.

- If a `label-sfx` is given then the `pri-int` gives
the sfx-id of the first proc inst to connect to.
If no `pri-int` is given then the first sfx-id 
defaults to 0. 


- If "_" is given:
	+ This is an "iterating" src-proc and therefore
	  the in-var must also be iterating. See (0)

	+ If a `sec-int` is given then this gives the count of
      connections across multiple proc instances with
	  sfx-id's beginnign with `pri-int`.  Note that if
	  `sec-int` is given then the `in-var-id` must be
	  iterating and NOT specify an iteration count,
	  as in (1) above.
	  
	+ If no `sec-int` is given then the 
	  `sec-int` defaults to the count of
	  available proc instances with the given `label` 
	  following the source proc inst `pri-int`.



- If "_" is not given then this is not an
  iterating proc inst. 
  
    + If the input var is iterating
      then it must specify the iteration count or
	  the `src-var-id` must be iterating.
	  
	+ If the `pri-int` is given then it specifies
	  the sfx-id of the src-proc
	  
	+ If the `pri-int` is not given 
	
	    - If the src-net is the same as the in-var net then
	      the sfx-id of the in-var proc is used as the src-proc sfx-id


### `src-var-id`

- The `label` part of the `in-var-id`  must match to a 
var description in the source proc class descriptions.

- If a `label-sfx` is given then the `pri-int` gives
the sfx-id of the first source var to connect to
on the source proc instance. If no `pri-int` is
given then the first sfx-id defaults to 0.

- If a "_" is given:
    + This is an "iterating"
	  source var and therefore the input var
	  must specifiy an iterating connection and
	  the source proc inst must not specify an iterating 
	  connection. See (0) above.

	+ If a `sec-int` is given then this gives the count of
      connections across multiple source vars with
	  sfx-id's beginnign with `pri-int`.  Note that if
	  `sec-int` is given then the `in-var-id` must be
	  iterating and NOT specify an iteration count,
	  as in (1) above.


	+ If `sec-int` is not given
	  then the `sec-int` defaults to the count of
	  available source vars with the given `label` 
	  following the source var `pri-int`.

- If "_" is not given then this is not an
  iterating source var. If the input var is iterating
  then it must specify the iteration count or
  the `src-proc-id` must be iterating.


### Notes:

- If the `in-var-id` is iterating but neither `src-proc-id`
or `src-var-id` are iterating then the `in-var-id` must
specify the iteration count and the connection will be
made to exactly one source var on the source proc inst.

- If `in-var-id` is iterating then the iterations count
must come from exactly one place:
    + the input var `sec-int`
	+ the source proc `sec-int`
	+ the source var `sec-int`
	
This means that only one literal iter count can be 
given per `in-stmt`.  It is a syntax error if
more than one literal iter counts are given.

- Use cases
    + connect one input to one source
	+ connect multiple inputs to the same var on multiple procs
	+ connect multiple inputs to multiple vars on one proc
	+ connect multiple inputs to one var on one proc


### in-stmt Examples:

`in:sproc.svar`   Connect the local variable `in` to the source variable `sproc.svar`.

`in0:sproc.svar`  Create variables `in0` and connect to `sproc.svar`.

`in_2:sproc.svar` Create variables `in0` and `in1` and connect both new variables to `sproc.svar`.

`in_:sproc.svar0_2` Create variables `in0` and `in1` and connect them to `sproc.svar0` and `sproc.svar1`.

`in3_3:sproc.svar` Create variables `in3`,`in4` and `in5` and connect them all to `sproc.svar`.

`in_:sproc.svar1_2` Create variables `in0`,`in1` and connect them to `sproc.svar1` and `sproc.svar2`.

`in1_2:sproc.svar3_` Create variables `in1`,`in2` and connect them to `sproc.svar3` and `sproc.svar4`.

`in_:sproc.svar_` Create vars `in0 ... n-1` where `n` is count of vars on `sproc` with the label `svar`.
`n` is called the 'mult-count' of `svar`.  The new variables `in0 ... n-1` are also connected to `sproc.svar0 ... n-1`.

`in_:sproc_.svar` Create vars `in0 ... n` where `n` is determineed by the count of procs named `sproc`.
`n` is called the 'poly-count' of `sproc`. The new variables `in0 ... n-1` are also connected to `sproc.svar0 ... n-1`

If an underscore precedes the in-var then this implies that the connection is being
made from a poly context. 

`foo : { ... in:{ _.in:sproc.svar_ } ... } ` This example shows an excerpt from the network 
definition of proc `foo` which is assumed to be a poly proc (there are multiple procs named 'foo' in this network).
This connection iterates across the procs  `foo:0 ... foo:n-1` connecting the 
the local variable 'in' to `sproc.svar0 ... n-1`. Where `n` is the poly count of `foo`.

`foo : { ... in:{ 1_3.in:sproc.svar_ } ... }` Connect `foo:1-in:0` to `sproc:svar0` and `foo:2-in:0` to `sproc:svar1`.

`foo : { ... in:{ 1_3.in:sproc_.svar } ... }` Connect `foo:1-in:0` to `sproc0:svar0` and `foo:2-in:0` to `sproc1:svar`.

#### in-stmt Anti-Examples

`in_:sproc_.svar_` This is illegal because there is no way to determine how many `in` variables should be created.

`in:sproc.svar_` This is illegal because it suggests that multiple sources should be connected to a single input variable.

`in:sproc_.svar` This is illegal because it suggests that multiple sources should be connected to a single input variable.

`_.in_:sproc.svar` This is illegal because it suggests simultaneously iterating across both the local proc and var.
This would be possible if there was a way to separate how the two separate iterations should be distributed 
to the source. To make this legal would require an additional special character to show how to apply the poly
iteration and/or var iteration to the source. (e.g. `_.in_:sproc*.svar_`) 

### out-stmt Examples:

`out:iproc.ivar` Connect the local source variable `out` to the input variable `iproc:ivar`.

`out:iproc.ivar_` Connect the local source variable `out` to the input variables `iproc:ivar0` and `iproc:ivar1`.

`out_:iproc.ivar_` Connect the local souce variables `out0 ... out n-1` to the input variables `iproc:ivar0 ... iproc:ivar n-1`
where `n` is the mult count of the `out`.

`out_:iproc_.ivar` Connect the local souce variables `out0 ... out n-1` to the input variables `iproc0:ivar ... iproc n-1:ivar`
where `n` is the mult count of the `out`.


`_.out:iproc.ivar_` Connect the local source variables `foo0:out`, `foo n-1:out` to the input variables `iproc:ivar0`, `iproc:ivar n-1`.
where `n` is the poly count of `foo`.



Var Updates and Preset Application
==================================

Variable addresses are formed from the following parameters:
`(<proc_label><proc_label_sfx_id>)*,var_label,var_label_sfx_id, ch_idx`

In the cases of poly procs (procs with public internal networks) it
may not always be possible to know the `<proc_label_sfx_id>` without
asking for it at runtime.  For example for the cross-fader control the
application must ask for the `<proc_label_sfx_id>` current or next
poly channel depending on which one it is targetting.

It is notable that any proc with an internal network has
to solve this problem.  The problem is especially acute
for proc's which change the 'current' poly channel at runtime.

The alternative is to include the concept of special values 
in the address (e.g. kInvalidIdx means the application isn't
sure and the network should decide how to resolve the address)
The problem with this is that the information
to make that decision may require more information than
just a simple 'special value' can encode. It also means
complicating the var set/get pipeline with 'escape' routines.

There are at least two known use cases which need to address 
this issue:
1. The cross-fader: The application may wish to address
  updates to the current or next poly channel but this
  channel can't be determined until runtime.

- The application asks for the current or next `proc_label_sfx_id`
  at runtime depending on what its interested in doing, 
  and sets the update address accordingly.
  
  
- Two interface objects are setup as sources for the `xfade_ctl` 
  object.  The address of each of these objects can be 
  determined prior to runtime. The application then simply addresses
  the object corresponding to method (direct vs deferred) it requires.
  This solution is particularly appealing because it means that
  presets may be completely resolved to their potential 
  target procs (there would be up to 'poly-count' potential targets)
  prior to runtime.
  
  As it stands now the problem with this approach is that it
  does not allow for the message to be resolved to its final
  destination. If the message is addressed to a proxy proc
  then that proxy must mimic all the vars on the object which
  it is providing an interface for. (This is actually possible
  and may be a viable solution???) 
    
  One solution to this is to create a data type which is an
  address/value packet.  The packet would then be directed
  to a router which would in turn use the value to forward
  the packet to the next destination.  Each router that the
  packet passed through would strip off a value and
  pass along the message. This is sensible since the 'value'
  associated with a router is in fact another address.
  
2. The polyphonic sampler: 

- How can voices be addressed once they are started?
  + A given note is started - how do we later address that note to turn it off?
    Answer: MIDI pitch and channel - only one note may be sounding on a given MIDI pitch and channel at any one time.
	
    - Extending ths idea to the xfader: There are two channels: current and deferred,
	but which are redirected to point to 2 of the 3  physical channels .... this would
	require the idea of 'redirected' networks, i.e. networks whose proc lists were
	really pointers to the physical procs.
	  - sd_poly maintains the physical networks as it is currently implemnted.
	  - xfade_ctl maintains the redirected networks - requests for proc/var addresses 
	    on the redirected networks will naturally resolve to physical networks. 

	  - Required modifications:
	      + variable getters and setters must use a variable args scheme specify the var address:
		    `(proc-name,proc-sfx-id)*, var-name,var-sfx-id`
			Example: `xfad_ctl,0,pva,1,wnd_len,0,0`
	            - The first 0 is known because there is only one `xfad_ctl`.
				- The 1 indicates the 'deferred' channel.				
				- The second 0 is known because there is only one `wnd_len` per `pva`.
				- The third 0 indicates the channel index of the var.
			
		  + the address resolver must then recognize how to follow internal networks
		      
		  + Networks must be maintained as lists of pointers to procs
		    rather than a linked list of physical pointers.
			
		  + `xfade_ctl` must be instantiated after `sd_poly` and be able
			  to access the internal network built by `sd_poly`. 
		  
	
Generalizing the Addressing
---------------------------
Change the set/get interface to include a list of (proc-label,proc-sfx-id) 
to determine the address of the var. 

Note that this still requires knowing the final address in advance.
In general a router will not know how to resolve a msg to the 
next destination without having a final address.
In otherwords setting 'proc-sfx-id' to kInvalidId is not
resolvable without more information.

   
   

### TODO:

- Check for illegal variable names in class descriptions.  (no periods, trailing digits, or trailing underscores)
- Check for unknown fields where the syntax clearly specifies only certain options.

- Class presets cannot address 'mult' variables (maybe this is ok since 'mult' variables are generally connected to a source.).

- Documentation w/ examples.
  + Write the rules for each implementing member function.
    
- value() should return a special return-code value to indicate that the
value should not be updated and distinguish it from an error code - which should stop the system.

- flow classes and variable should have a consistent naming style: camelCase or snake_case.

-  String assignment is allocating  memory:
   See: `rc_t _val_set( value_t* val, const char* v ) cwFlowTypes.cpp line:464.`


- Variable attributes should be meaningful. e.g. src,src_opt,mult,init, ....
  Should we check for 'src' or 'mult' attribute on var's?
  (In other words: Enforce var attributes.)

- Reduce runtime overhead for var get/set operations.
  
- Implement matrix types.

- Should the `object_t` be used in place of `value_t`?

- Allow min/max limits on numeric variables.

- log: 
    + should print the values for all channels - right now it is only
	printing the values for kAnyChIdx
	+ log should print values for abuf (mean,max), fbuf (mean,max) mag, mbuf



- DONE: Compile presets: at load time the presets should be resolved
  to the proc and vars to which they will be assigned.


- DONE: (We are not removing the kAnyChIdx) 
Should the var's with multiple channels remove the 'kAnyChIdx'?
This may be a good idea because 'kAnyChIdx' will in general not be used
if a var has been channelized - and yet it is possible for another 
var to connect to it as a source ... which doesn't provoke an error
but would almost certainly not do what the user expects.
Note that the kAnyChIdx provides an easy way to set all of the channels
of a variable to the same value.


- DONE: verifiy that all proc variables values have a valid type - (i.e. (type & typeMask) != 0)
  when the proc instance create is complete. This checks that both the type is assigned and
  a valid value has been assigned - since the type is assigned the first time a value is set.
  
- DONE: 'poly' should be implemented as a proc-inst with an internal network - but the 
elements of the network should be visible outside of it.

- DONE: 'sub' should be implemented as proc-inst with an internal network, but the
elements of the network should not be visible outside of it. Instead it should
include the idea of input and output ports which act as proxies to the physical
ports of the internal elements.

- DONE: 'poly' and 'sub' should be arbitrarily nestable. 

- DONE: Allow multiple types on an input.
   For example 'adder' should have a single input 
   which can by any numeric type.
   

- DONE: Make a standard way to turn on output printing from any port on any instance
This might be a better approach to logging than having a 'printer' object.
Add proc instance field: `log:{ var_label_0:0, var_label_1:0 } `

Next:

- Complete subnets:
	+ Subnets should have presets written in terms of the subnet vars rather than the network vars
	or the value application needs to follow the internal variable src_var back to the proxy var.

	+ DONE: write a paragraph in the flow_doc.md about overall approach taken to subnet implementation.

	+ DONE: subnet var desc's should be the same as non+subnet vars but also include the 'proxy' field.
	In particular they should get default values.
	If a var desc is part of a subnet then it must have a proxy.
	The output variables of var desc's must have the 'out' attribute


	+ DONE: improve the subnet creating code by using consistent naming + use proxy or wrap but not both

	+ DONE: improve code comments on subnet creation

- Audio inputs should be able to be initialized with a channel count and srate without actually connecting an input.
  This will allow feedback connections to be attached to them at a later stage of the network 
  instantiation.
- Remove the multiple 'args' thing and and 'argsLabel'.  'args' should be a simple set of arg's.  
- Implement subnet preset application.	
- Implement the var attributes and attribute checking.
- Implement dynamic loading of procs.
- Implement a debug mode to aid in building networks and subnets (or is logging good enough)
- Implement multi-field messages.
- Look more closely at the way of identify an in-stmt src-net or a out-stmt in-net.
It's not clear there is a difference between specifying  `_` and the default behaviour.
Is there a way to tell it to search the entire network from the root? Isn't that 
what '_' is supposed to do.
  
- DONE: Implement feedback	

- DONE: Implement the ability to set backward connections - from late to early proc's.
  This can be done by implementing the same process as 'in_stmt' but in a separate 
  'out_stmt'. The difficulty is that it prevents doing some checks until the network
  is completely specified.  For example if audio inputs can accept connections from 
  later proc's then they will not have all of their inputs when they are instantiated.
  One way around this is to instantiate them with an initial set of inputs but then
  allow those inputs to be replaced by a later connection.

BUGS:
- The counter modulo mode is not working as expected.



Host Environments:
------------------
- CLI, no GUI, no I/O, non-real-time only.
- CLI, no GUI, w/ I/O and real-time
- GUI, with configurable control panels


- DONE: Implement 'preset' proc. This will involve implementing the 'cfg' datatype.

- DONE: Finish the 'poly' frawework. We are making 'mult' var's, but do any of the procs explicitly deal with them?

- DONE: Turn on variable 'broadcast'.  Why was it turned off? ... maybe multiple updates?

- DONE: There is no way for a proc in a poly context to use it's poly channel number to 
select a mult variable.  For example in an osc in a poly has no way to select
the frequency of the osc by conneting to a non-poly proc - like a list.
Consider: 
1. Use a difference 'in' statememt (e.g. 'poly-in' but the 
   same syntax used for connecting 'mult' variables.)
2. Include the proc name in the 'in' var to indicate a poly index is being iterated
   e.g. `lfo: { class:sine_tone, in:{ osc_.dc:list.value_ } }` 

- DONE: Fix up the coding language - change the use of `instance_t` to `proc_t` and `inst` to `proc`, change use of `ctx` in cwFlowProc



Prior to executing the custom constructor the values are assigned to the 
variables as follows:
1. Default value as defined by the class are applied when the variable is created.
2. The proc instance 'preset' class preset is applied.
3. The proc instance 'args' values are applied.

During this stage of processing preset values may be given for
variables that do not yet exist. This will commonly occur when a
variable has multiple channels that will not be created until the
custom constructor is run.  For these cases the variable will be
pre-emptively created and the preset value will be applied.  

This approach has the advantage of communicating network information
to the proc constructor from the network configuration - thereby
allowing the network programmer to influence the configuration of the
proc. instance.


DONE: After the network is fully instantiated the network and class presets
are compiled.  At this point all preset values must be resolvable to
an actual proc variable.  A warning is issued for presets with values
that cannot be resolved and they are disabled.  The primary reason
that a preset might not be resolvable is by targetting a variable
channel that does not exist.



- How much of the proc initialization implementation can use the preset compile/apply code?
- DONE: All cfg to value conversion should go through `cfg_to_value()`.


Names:
ixon - 
hoot
caw, screech, warble, coo, peep, hoot, gobble, quack, honk, whistle, tweet, cheep, chirrup, trill, squawk, seet, 
cluck,cackle,clack
cock-a-dooodle-doo
song,tune,aria


caw by example:
0. DONE: Add log object.
   DONE: Add automatic network preset selection.

1. sine->af
   in-stmt
   
2. sine->af with network preset
   topics:preset, class info

3. number,timer,counter,list,log
   topics: log, data types, system parameters

4. sine->delay->mixer->af
       -------->
   topics: mult vars, system parameters
   
5. topic: modulate sine with sine

6. topic: modulate sine with sine and timed preset change.

7. topic: iterating input stmt 0 - connect multiple inputs to a single source
          
8. topic: iterating input stmt 1 - multiple inputs to multiple sources

9. topic: iterating input stmt 2 - two ranges

12. topic: poly

13. topic: poly w/iterating input stmt

14. topic: poly w/ xfade ctl and presets

15. topic: msg feedback

16. topic: audio feedback

17. topic: subnets

18. topic: subnet with presets

19. topic: presets w/ sfx id's











   
   
   
