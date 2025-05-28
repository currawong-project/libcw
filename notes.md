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

Notes:
- All vars must be included in the class description.
- All vars have a 'kAnyChIdx' instantiation. The kAnyChIdx variable serves two purposes:
  + Setting the value of  kAnyChIdx automatically broadcasts the value to all other channels.
  + kAnyChIdx acts as a template when variables are created by 'channelization'.
    This allows the network designer to set the value of the kAnyIdx variable
	and have that become the default value for all subsequent variables
	which are created without an explicit value.  (Note that his currently
	works for variables created from within `proc_create()` (i.e.
	where `sfx_id` == kBaseSfxId, but it doesn't work for mult 
	variables that are automatically created via `var_register()`
	because `var_register()` does not have a value to assign to the 
	kAnyChIdx instance. In this case the variable get assigned
	the class default value.  The way around this is to explicitely
	set the mult variable value in the 'in' stmt or the 'args' stmt.)
	
	
	


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

- Is it possible to create a processor where the output ports are defined by a cfg contained as part of the
  processor instance description?  The current solution is the 'msg_table' approach. Can this approach
  be improved so that it is codeless - as opposed to having to create a processor in proc_dict_cfg.
  Techniques used by User-defined-procs might offer one path - the idea being to create a class_desc
  at compile time.

- Check for duplicate field labels. Particularly when using a base record.
  It's easy to declare a field named 'midi' and then inherit a record with a field named 'midi' - this is a crash bug.
  

- DONE: Why doesn't the C7 on the downbeat of meas. 11 sound? (... it does but is quiet due to velocity table)
- DONE: Allow setting the location of the score player.  This should also reset the sampler and voice control.
- DONE: The voice ctl should respond to all-notes-off message and reset each sampler channel.

- Log updates:
  + Consider the ability to set colors to the log output based on the log level value.
  + Allow printing to the console from the CAW app during idle time.

- Variable value list updates:
  - Currently lists are created and destroyed by the proc.
    (The proc should continue to create the list, but the variable should be responsible for destroying it.
     This will mean that 'variable_t.value_list' will no longer be able to be const.
     
  - Variables with lists can be any type and there is no checking that the list value matches the variable type.
    (The list value type and the variable value type should be verified as compatible when the list is assigned to the variable.)
    
  - The proc receives list changes as uint's and then must manualy lookup the value and then set the value of the variable - see 'gutim_ps'.
    (This should all be automatic.  The list value should be automatically dereferenced the variable updated without having to do any special handling in the proc.)

    + In cwFlowTypes.h add:
       - set_var_from_list( var, index ) - this must be implemented in terms of set_var(var,value) so that the value goes through the normal value update pipeline.
       - get_var_list_index( var )  - return the index of the current value from the list

       - Similar functions will need to be added to cwIoFlowCtl,cwFlow,cwFlowNet to allow the UI to interact in terms of list indexes.
        
    
  - The default value of a list is always the element at index 0.
    (A default value should be set when the list is created - and it should be any value in the list.)

  - When a list value is set via a connection the value could be optionally validated in the list.

  - The existence of a valid 'variable_t.value_list' could be enough to indicate that a UI element should be list widget
    without having to also set the `ui:{ type:list }` field.

  - Notes:
    + There is no way for an external proc to know the contents/length of the list and therefore it would never make sense
      to expose the list index as a potential input port.  This kind of thing would be better handled via a 'list' proc.
      where the list is internal to the proc.

    +  


- All outputs must be set via var_set() call, otherwise proc's that rely on noticing changed variables
will not work.  For example the 'print' proc does not work for record,midi,audio because
in general these types are not set via calls to var_set().

- The following two tasks need more consideration. As it is variables assume that aggregate
types are destroyed on exit.  This is very convenient. Consider using 'symbols' to represent
strings, consider adding a 'const-string' type to eliminate memory allocation of string assignment
  - Memory allocation of all non-integral types should be the responsibility of the the processor instances
  this includes strings.  Change the built-in string type to be a const string and
  make it the responsibility of the proc instances with 'string' var's to handle
  alloc and dealloc of strings.

  - String assignment is allocating  memory:
     See: `rc_t _val_set( value_t* val, const char* v ) cwFlowTypes.cpp line:464.`

- DONE Presets do not work for hetergenous networks.

- Add a proc class flag: 'top-level-only-fl' to indicate that a processor
cannot run as part of a poly. Any processor that calls a global function,
like 'network_apply_preset()' must run a the top level only.

- DONE: Consider eliminating the value() custom proc_t function and replace it by setting a 'delta flag' on the
variables that change.   Optionally a linked list of changed variables could be implemented to
avoid having to search for changed variable values - although this list might have to be implemented as a thread safe linked list.

- DONE: value() should return a special return-code value to indicate that the
value should not be updated and distinguish it from an error code - which should stop the system.
Note: This idea is meaningless since variables that are set via connection have no way to 'refuse'
a connected value - since they are implemented as pointers back to the source variabl.e

- The 'two slot' approach to setting variable no longer seems useful.
The only reason not to eliminate it is to possibly use it as a way to test local values
before they are set, but it isn't clear if this actually useful.  Consider the case
of setting min/max for numeric values - could it be used there?


- DONE: Allow proc's to send messages to the UI. Implementation: During exec() the proc builds a global list of variables whose values
should be passed to the UI. Adding to the list must be done atomically, but removing can be non-atomic because it will happen
at the end of the network 'exec' cycle when no proc's are being executed.  See cwMpScNbQueue push() for an example of how to do this.

- Allow min/max limits on numeric variables.

- DONE: Add a 'doc' string-list to the class desc.

- Add 'time' type and 'cfg' types to a 'log:{...}' stmt.

- print_network_fl and print_proc_dict_fl should be given from the command line.
  (but it's ok to leave them as cfg flags also)
  
- Add 'doc' strings to all proc classes.

- Add 'doc' strings to user-defined proc data structure.

- DONE: It is an error to specify a suffix_id on a poly network proc because the suffix_id's are generated automatically.
  This error should be caught by the compiler.
  
- How do user defined procedures handle suffix id's?

- DONE: Add a 'preset' arg to 'poly' so that a preset can be selected via the owning network.
  Currently it is not possible to select a preset for a poly.

- DONE: Automatic assignment of sfx_id's should only occur when the network is a 'poly'.
  This should be easy to detect.
  
- DONE: If a proc, inside a poly,  is given a numeric suffix then that suffix will
overwrite the label_sfx_id assigned by the system.  This case should be detected.

- When a var value is given to var_create() it does not appear to channelize the
var if value is a list.  Is a value ever given directly to `var_create()`?
Look at all the places `var_create()` is called can the value arg. be removed?

- var_channelize() should never be called at runtime.

- DONE: Re-write the currawong circuit with caw.

- Finish audio feedback example - this will probably involve writing an `audio_silence` class.
  Update: there is now an audio_silence class.

- Issue a warning if memory is allocated during runtime.

- cwMpScNbQueue is allocating memory.  This makes it blocking.
  Update: 3/25 - this queue is not currently used

- Check for illegal variable names in class descriptions.  (no periods, trailing digits, or trailing underscores)

- DONE: Check for unknown fields where the syntax clearly specifies only certain options via the 'readv()' method.

- Verify that all variables have been registered (have valid 'vid's) during post instantiation validation.
  (this is apparently not currently happening)
  
- How is the number of channels for a given variable determined?  Is it the widest (max channel) preset
  that is encountered during preset compilation?  What if a variable has a wide preset but it is not
  initially applied - does that mean an uninitialized channel is just sitting there? (... no i think
  the previous channel is duplicated in var_channelize())

- The IO system can use audio files as audio devices.  Document this!

- UI Issues:
    + When UI appIdMap[] labels do not match ui.cfg labels no error is generated. All appIdMap[] labels should be
      validated to avoid this problem.

    + The reliance on using UUId to build UI's should be eliminated. It is very clunky.

    + UI elements should form proper tree's where elements know their children. As it is the links only go up the tree
      from child to parent - searching down the tree is not possible.

    + Disabled "disp_str" should turn grey.

    + mult var's with more than 3 values should be put into a list or use a 'disclose' button

    + mult proc's with more than 3 instances should be put into a list or use a 'disclose' button

    + add a UI label to the flow var description



- Class presets cannot address 'mult' variables. Maybe this is ok since 'mult' variables are generally connected to a source?
... although 'gain' mult variables are not necessarily connected to a source see: `audio_split` or `audio_mix`.
Has this problem been addressed by allowing mult variables to be instantiated in the 'args' statement?

- Write processor development documentation w/ examples.
  + Write the rules for each implementing member functions.
    

- flow classes and variable should have a consistent naming style: camelCase or snake_case.


- Variable attributes should be meaningful. e.g. src,src_opt,mult,init, ....
  Should we check for 'src' or 'mult' attribute on var's?
  (In other words: Enforce var attributes.)
  + A variable with the 'init' flag should never be changed at runtime.

- How much of the proc initialization implementation can use the preset compile/apply code?

- Reduce runtime overhead for var get/set operations.
  

- Should the `object_t` be used in place of `value_t`?


- log: 
    + should print the values for all channels - right now it is only
	printing the values for kAnyChIdx
	+ log should print values for abuf (mean,max), fbuf (mean,max) mag, mbuf

- Audio inputs should be able to be initialized with a channel count and srate without actually connecting an input.
  This will allow feedback connections to be attached to them at a later stage of the network 
  instantiation.

- The signal srate should determine the sample rate used by a given processor.
The system sample rate should only be used a default/fallback value.
Processors that have mandatory signal inputs should never need to also have an srate parameter.
Consider a network with a variable sample rate.
  
- Implement user-defined-proc preset application.	

- Implement the var attributes and attribute checking.

- Port 'cm' and 'hum' processors.

- Implement Linux audio plugins loading

- Implement dynamic loading of procs.

- Implement a debug mode to aid in building networks and user-defined-procs (or is logging good enough)

- DONE: Implement multi-field messages.

- Implement user defined data types.

- Implement matrix types. ... pick a library.

- Add a 'trigger' data type. The 'kAllTId' isn't really doing anything.
Perhaps this  could be a 'symbol' data type?

- There should be special logging macros inside procs that automatically log the instance name.
  Likewise there should be special cwLogErrorProc(rc,proc), cwLogErrorVar(rc,var) to automatically report the source of the error.

- Look more closely at the way to identify an in-stmt src-net or a out-stmt in-net.
It's not clear there is a difference between specifying  `_` and the default behaviour.
Is there a way to tell it to search the entire network from the root? Isn't that 
what '_' is supposed to do?

- cwAudioFile cannot convert float or double input samples to 24 bit output samples
See audiofile::writeFloat() and audiofile::writeDouble().


Host Environments:
------------------
- CLI, no GUI, no I/O, non-real-time only.
- CLI, no GUI, w/ I/O and real-time
- GUI, with configurable control panels


Proc instantiation
------------------
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


caw by example:
---------------
0. DONE: Add log object.
   DONE: Add initial network preset selection system parameter.

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


caw w/ UI
---------

1. If no program is given, but a cfg file is given, then load a menu with the pgm's in the file.
   Selecting a prg name then loads the pgm
   Otherwise, if a pgm is given then the pgm is automatically loaded.
      If the pgm does not set 'use_ui_fl' then it is automatically initialized and executed.
      Otherwise goto step 2

   

2. Once a pgm is loaded then it can be queried for UI information.
  - get basic information from proc dict
  - get override information from the network
  

Network Execution:
------------------

1. During real-time execution the network is executed on callbacks from the audio subsytem.
These callbacks occur asynchronously on the IO system audio processing thread.

2. During real-time processing the MIDI callbacks are also asynchronous.



Network Architecture and Theory of Operation
---------------------------------------------

A _caw_ graph is an ordered set of processors where a given
processor may contain a set of internal networks.
This leads to a heirarchy of networks and processors
that can be depicted like this.

[ diagram goes here.]

This diagram shows a two level network, where the internal
network contains an array of networks.

A processor instance is structured as a collection of variables along with a small set of
functions for maintaining it's state.
Variables act both as a means of holding the time varying state of the processor
and as input and output ports.  There is nothing preventing a variable from
being both an input and output port, although in practice then tend to be one
or the other.

As is the case in most dataflow implementations processors act as the nodes
of the graph and edges represent the flow of information between output and
input variables.  A _caw_ graph allows fan-out, multiple outputs from a variable,
but not fan-in.  A variable acting as in input may have only a single incoming edge.


Networks are executed sequentially, one processor at a time, from top
to bottom.  Networks that are members of the same network array,
referred to as silbing networks, may however execute concurrently to
one another.  To avoid concurrency hazards sibling networks may
therefore not contain any inter-connnections, and the language
precludes them.

There are two primary thread hazards to this arrangment:
1. Processors executing outside of the top level
should not write to global data or interact with
the global system API.

There is no way for the top level of a network to be a sibling
network, therefore processors that are part of this network are
guaranteed to run in sequence and be the only processor running while
they are executing.

For example, network presets can only be applied between execution
cycles (by the control application) or by a top-level processor.  This
is the case because it guarantees that no processors are running when
the preset values are set.


/*********************************
2. If a processor receives data from a sibling network it is possible
that the processors value() function is called from multiple
concurrent threads.  Processors which receive data from sibling
networks (e.g. audio_mixer, poly_xform_ctl) should either not
implement value() functions or be thread aware in their handling of
calls to value().

Depending on the nature of the processing in the value() function
this may not be particularly problematic since a given variable may
only be connected to a single source.  While the value() function
may be called from multiple overlapping threads the arguments
to each thread will refer to a unique variable.  The built-in variable update
process is carefully designed to exploit this invariant and not modify
any process state with the exception of the targetted variable itself.

The danger in value() function processing is in writing to any process
state, or any other variable than the one being reported as changing.
Likewise it should be recognized that even reading the value of other
variables should be done with caution.  Reading other variables is
thread safe in the sense that the internal state of the processor will
be safe to traverse. The actual value of other variables however may be
inconsistent relative to one another, and not the same as when the
processors exec() function eventually runs - since they too may be in
the process of being updated.

The purpose of the value() function is to provide a single
easy way of picking up changed incoming values without
having to test for changed values in the exec() function.
It shouldn't be used as an alternate exec() function.
*******/


Note that the create() and destroy() calls for all processors
in the entire graph occur in a single thread and therefore do
not need to take multi-thread precautions - at least relative
to other _caw_ based execution.

Records
-------

The primary reason to use a 'record' data type is to allow multiple
data values to be transmitted during a single cycle and be received as
a single incoming value.  In effect a record allows a structured table
of values to be transmitted during a single execution cycle.  For
example let's say that the output of a processor (G) is an irregular pulse
whose rate might be faster than the audio frequency.  This would
require multiple pairs of value (delta-time,amplitude) to be generated
during a given cycle.

Without the use of the record data type this would require that the generating
processor have two output variables 'dtime' and 'amplitude' which
would be updated multiple times during a single execution cycle.  The
receiving processor (R) would then need to respond to each of those
changes by implementing a value() function and storing the incoming
values in an internal array.  The stored values could then be acted
upon during the receiving processors exec() function.

If R didn't take this approach, and simply read
the incoming variables at the beginning of it's own execution cycle,
it would only see the value of the two output variables as they were left at the
end of the G execution cycle.  The previous values
transmitted during the execution cycle would be lost.

By explicitely transmitting a record G makes clear that multiple
values may be transmitted during a single execution cycle, while
providing the convenience R of automatically storing those value.
Furthermore the tranmission overhead is minimized by only transmitting
a single aggregate value rather than multiple individual values.

Another example, would be MIDI values that contain some additional
side information.  The MIDI data type already has the feature that it
can generate multiple messages per execution cycle. It's format
however is fixed. There is no way to add addtional information, like
score location, to each message. The fields of the record data type
however can hold any of the other data types.

Another reason to use the record data type is to simplify
the output and input interfaces to a processor and thereby
decrease the number of connections between processors.
They also make clear that a set of values is synchronized in time.
For example a set of (x,y) coordinates placed in a record
make it clear that the two values belong together in a way
that two input variables may not.

Finally, records are also very efficient. Given that the field index is
computed in advance setting and getting the field variable
is very fast.  As mentioned above transmitting a record avoids the overhead
of notifying receiving processors of every new value. The
receiving processor is only notified that the record
as a whole has changed.

Records are also implented in such a way that appending
a additition fields to an existing record is very fast.
The new record effectively inherits the contents of the
existing record by reference.  No data is copied.
For an example of this see the `vel_table` implementation.

Note that record field values may not be changed in place because
this would change the value for all other processors that
receive the record.  Incoming records must therefore be
copied if they are changed.

Variable Change Notification
----------------------------
Processors are not directly notified when one of their connected variables changes.
This is the case because _caw_ does not implement a 'message' or 'event'
propagation scheme.  When an 'input' variable changes the processor which owns
it is not informed of the change.  This limitation exists to prevent the need
for a processor to implement a thread-safe message handling scheme - since it is
possible that variable updates would come from concurrent threads executing
the processors to which precede it in the graph.

By default if a processor needs to notice
when an input variable changes it needs to cache the value and check
if the value has changed on the next execution cycle.  For a processor with
many rarely changing variables this can be both messy and wasteful since it is possible
that most of the variable will not change on any given execution cycle.

To handle this case a variable change notfication scheme is built into the system.
When a variable description is marked with the 'notify' tag it is placed on
an internal list (proc_t.modVarMapA[]).  Just prior to the processors instance
exec() cycle a call to proc_notify() will result in a callback to the
instances notify() function for every marked variable that was modified since
the last execution of the instance.  Note that these callbacks happen
from a single thread, the same one that will subsequently call the exec() function.

The calls to notify() occur in the same order that the variables changed in time,
however there will be only one callback per variable.  If a variable changed
multiple times during the cycle only the last value will be recorded.
This limitation however exists for all processor input variables whether they
use the notification scheme or not.

Note that when the value of a variable that is marked for notification is changed
inside the notify() callback of the proc with owns it a new notification will not be
triggered.  A processor will never trigger a notification on any of it's own variables.

Optional Variables
-------------------

The current design does not allow for optional variables.
At processor instantiation all defined variables must exist and have a
valid value. In generate this is a good thing because it
means that call to get_var() will always return a value.

midi_out has implemented 'in' and 'rin' as optional
variables. Look there for an example of how to accomplish this.

Threading:
--------

### Application Thread

The application thread is the thread provided by the main() function.
During the life of the program it runs in a loop and continually
calls the io::exec() function.
Exec:
   If a UI exists calls ui::ws::exec() to do websocket IO tasks.
     - This call may block, up to a programmable time-out duration, waiting for incoming websocket messages.
     - UI related callbacks to the application occur from this context.
     - If cfg. does not set the 'ui.asyncFl' then this thread may block on the IO callback mutex.

  If the audio meters are enabled then this these callbacks occur from this context.
     - If the audio group 'asyncFl' is not set then this call may block on the IO callback mutex.

  An 'exec' callback to the IO callback is made on every call to io::exec().
     - This call will block if the IO callback mutex is not available.


### Audio/DSP Thread
   Each audio device group uses a thread to coalesce device 'ready' states. When all devices in the group are ready
   (have empty playback and full record buffers) the thread unblocks.
   
### MIDI
    cwMidiDevice uses a thread to wait for incoming MIDI messages via poll(). (See cwMidiDevice.cpp:_thread_func())
    - Incoming MIDI messages are fed to the IO callback via cwIo::_midi_callback().
    

### Internal Server Threads
    
#### User threads
     - Thread machine threads which are created (See cwIo::threadCreate()) and run by the user - and only destroyed when the library is closed.

#### Thread-Once
    - Thread which is created, executes immediately by calling a user supplied callback function, and is later cleaned up by io::exec().
      This is useful for long running procedures where feedback (logs and/or progress) from the procuedure is useful to see as the procedure executes.
    
#### Timer
    - Thread machine hosted threads which block with a sleep call.
    
#### Sockets
    - Uses a thread machine based thread to wait for incoming socket events.  (See cwIo.cpp:_socketCallback())
    
#### Serial
    - Based on cwSerialPortSrv which maintains it's own internal thread (See cwSerialPortSrv::threadCallback()) which blocks on the serial port file descriptors
    (See cwSerialPort.cpp:_poll())


Presets:
----------------

Preset description and application without the presence of 'poly' proc's is very straight forward:

For example:

```
example_1:
{

  network: {

    procs: {
      lfo:   { class: sine_tone, args:{ hz:3, dc:440, gain:110 }
        presets:
	{
	  ps_a:{ hz:2, dc:220, gain:55 },
	  ps_b:{ hz:4, dc:110, gain:220 },
	}
      }
      
      sh:    { class: sample_hold, in:{ in:lfo.out } }
      osc:   { class: sine_tone,   in:{ hz:sh.out },   args:{ ch_cnt:2 } },
      gain:  { class: audio_gain,  in:{ in:osc.out },  args:{ gain:0.3 } },
      aout:  { class: audio_out,   in:{ in:gain.out }, args:{ dev_label:"main"} }
    }
	
    presets:
    {
       a: { gain:{ gain:0.2 } },        //  One value sets both channels.
       b: { gain:{ gain:[0.1,0.3] } },  //  Multi-channel preset.
       c: { osc:a880 } },               //  Apply a class preset
       d: { osc:mono } },               //  Apply a class preset with an ignored 'init' variable.
       f: { osc:a220, lfo:ps_a } },     //  Apply a local preset and class preset
    }
  }
}
```

Calling `network_apply_preset(preset_label)` with one of the network preset labels 'a'-'f' will
work as expected in all of these cases.

Notes:

1. All preset values and proc/var's can be resolved at compile time.

2. Applying the network can be accomplished by resolving the network preset_label to a network_preset_t
in network_t.presetA and calling flow::var_set() on each attached preset_value_t.

3. Proc preset labels, as used in presets 'c','d','e' in the example, are resolved by first looking
for the label in the processor instance configation and then in the processor class description.


---

The following example shows how the preset processor labels can use suffix notation to address a range of processors.

```
example_2:
 {
  network: {
    procs: {
      osc:    { class: sine_tone, args: { ch_cnt:6, hz:[110,220,440,880,1760, 3520] }},
      split:  { class: audio_split, in:{ in:osc.out }, args: { select:[ 0,0, 1,1, 2,2 ] } },

      // Create three gain controls: g:0,g:1,g:2 using the processor label numeric suffix syntax.
      g0: { class:audio_gain, in:{ in:split0.out0 }, args:{ gain:0.9} },
      g1: { class:audio_gain, in:{ in:split0.out1 }, args:{ gain:0.5} },
      g2: { class:audio_gain, in:{ in:split0.out2 }, args:{ gain:0.2} },
                  
      merge: { class: audio_merge, in:{ in_:g_.out } },
      out:   { class: audio_out, in:{ in:merge.out },  args:{ dev_label:"main" }}
    }

    presets: {
      a: { g_:   { gain:0.1 } }, // Use suffix notation to apply a preset value to g0,g1,g2.
      b: { g0_2: { gain:0.2 } }, // Use suffix notation to apply a preset value to g0 and g1.
      c: { g2:   { gain:0.3 } }, // Apply a preset value to g2 only.
    }
  } 
}
```

---


example_3: {
   
  network: {

    procs: {

      // LFO gain parameters - one per poly voice
      g_list:  { class: list, args: { in:0, list:[ 110f,220f,440f ]}},

      // LFO DC offset parameters - one per poly voice
      dc_list: { class: list, args: { in:0, list:[ 220f,440f,880f ]}},
          
      osc_poly: {
        class: poly,
        args: { count:3 },  // Create 3 instances of 'network'.
           
        network: {
          procs: {
            lfo:  { class: sine_tone,   in:{ _.dc:_.dc_list.value_, _.gain:_.g_list.value_ }  args: { ch_cnt:1, hz:3 }},
            sh:   { class: sample_hold, in:{ in:lfo.out }},
            osc:  { class: sine_tone,   in:{ hz: sh.out }},
         },
	 
	 presets:
	 {
	   a:{ lfo:{ gain:0.1 } },
	   b:{ lfo:{ gain:0.2 } },
	   c:{ lfo0_1: { gain:0.3 }, lfo2:{ gain:0.4 } },
	  
	 }
       }               
     }

     // Iterate over the instances of `osc_poly.osc_.out` to create one `audio_merge`
     // input for every output from the polyphonic network.
     merge: { class: audio_merge,    in:{ in_:osc_poly.osc_.out}, args:{ gain:1, out_gain:0.5 }},
     aout:  { class: audio_out,      in:{ in:merge.out }          args:{ dev_label:"main"} }
    }

    presets: {
      a:{ osc_poly:a, merge:{ out_gain:0.3 } },
      b:{ osc_poly:b, merge:{ out_gain:0.2 } },
      c:{ osc_poly:c, merge:{ out_gain:0.1 } },
    }

  }
}


1. If a poly preset processor label does not have a numeric suffix then it is applied to all instances.
The alternative to this rule is to use an '_' suffix to imply 'all' processors of the given name.

2. A preset in an outer network may not directly address a processor in an inner network, however
it may select a named preset in an inner network.

3. Rule 2 can be generalized to: Network presets may only address processors which it contains directly - not nested processors.
In the example the outer most presets may therefore address the 'osc_poly' presets by label, but not
the processors contained by 'osc_poly'.

---
Presets with hetergenous poly networks
```
    example_03:
    {
      network: {

        procs: {

          osc_poly: {
            class: poly,
	    
	    // For het-poly networks the 'count' value given
	    // in the top level proc is the default value for
	    // the poly-count for following networks.
	    // This value may be overriden in the network
	    // definition itself - as it is in this example.
            args: { count:2, parallel_fl:true }, 
           
            network: {
	      net_a: {
	        count: 4,  // override higher level 'count'
		
                procs: {
                  osc:  { class: sine_tone,   args:{ hz: 100 }},
                },
		
		presets:
		{
		  a: { osc:{ hz:110 } },
		  b: { osc:{ hz:120 } },
		}
	      },
	      
	      net_b: {
	        count 3, // override higher level 'count'
                procs: {
                  osc:  { class: sine_tone,   args:{ hz: 200 }},
                },
		
		presets:
		{
		  a: { osc:{hz:220} },
		  b: { osc:{hz:230} }
		}
	      }
            },

            presets: {
	      aa: { net_a:a, net_b:a },
	      bb: { net_a:b, net_b:b },
	    }
           
         }

         // Iterate over the instances of `osc_poly.osc_.out` to create one `audio_merge`
         // input for every output from the polyphonic network.
         merge: { class: audio_merge,    in:{ in_:osc_poly.osc_.out}, args:{ gain:1, out_gain:0.5 }
	   presets: {
	     a:{ gain:0.3 }
	     b:{ gain:0.2 }
	   }
	 },
         aout:  { class: audio_out,      in:{ in:merge.out }          args:{ dev_label:"main"} }

	}
	
	presets: {
          a:{ osc_poly1:aa,   merge:a },
          b:{ osc_poly0_2:bb, merge:b },
          c:{ osc_poly:bb,    merge:{ out_gain:0.1 } },
	  d:{ osc_poly0:bb,   merge:{ out_gain:0.05} }
	}
      }
    }   
```
---
Dual Presets

---
Dual Presets with poly networks.

---
Dual Presets with heterogenous poly networks

---
Presets with user defined processors

---
Presets with user defined processors in poly networks.

---
Presets with user defined processors containing poly networks.


---
Use 'interface' objects to intercept preset values so that they
can be processed before being passed on to a the object that
they represent.

'interface' object have the same interface as the object to which their 'class' argument
refers but do nothing other than pass the values to their output ports.

```
example_4:
{

  network: {

    procs: {

      lfoIF: { class: interface, args:{ class:sine_tone } },

      // put a modifier here

      lfo:   { class: sine_tone, in:{ hz:lfoIF.hz, dc:lfoIF.dc, gain:lfoIF.gain } }
      sh:    { class: sample_hold, in:{ in:lfo.out } }
      osc:   { class: sine_tone,   in:{ hz:sh.out },   args:{ ch_cnt:2 } },
      gain:  { class: audio_gain,  in:{ in:osc.out },  args:{ gain:0.3 } },
      aout:  { class: audio_out,   in:{ in:gain.out }, args:{ dev_label:"main"} }
    }
	
    presets:
    {
       a: { lfoIF: { hz:1, dc:110, gain:55 } },
       b: { lfoIF: { hz:2, dc:220, gain:110 } },
    }
  }
}
```

---

Preset Implementation:

All presets are resolved to (proc,var,value) tuples when the networks are created.
A given named network preset is therefore a list these tuples.
Applying the preset is then just a matter of calling
var_set(proc,var,value) for each tuple in the list.
This pre-processing approach mostly avoids having to do value parsing
or variable resolution at runtime.


Preset dictionaries have the following grammar:
```
<preset-dict> -> <preset-label> : { <proc-label>: (<proc-preset-label> | <value-dict>) }

<value-dict> -> { <var_label>:(<literal> | [ <literal>* ])
``

A preset is a named ('<preset-label>') dictionary.
The pairs contained by the dictionary reference processors (<proc-label>).
The value of each pair is either a dictionary of variable
values (<value-dict>) or a label (<proc-preset-label>).

The <value-dict> is a collection of literal values
which can be directly converted to (proc,var,value) tuples.
In the case where a value is a list of literals the
individual values are used to address successive channels.
As part of variable resolution new variable channels will
be created if a preset references a channel that does
not yet exist on the given variable.  This guarantees
that the variable channel will be valid should the preset
be applied.

When the value of a preset pair is a <proc-preset-label>
the label may refer to one of three possible
source of preset variable values.
1. Processor class preset. This is a named <value-dict> defined with the processor class description.
2. Processor instance preset. This is a named <value-dict> defined with the processor instance.
3. Poly processor network preset. If the <proc-label> associated with
this <proc-preset-label> is a 'poly' processor then this label refers
to a network preset defined within the 'poly'  instances. 

Note that both <proc-label> and <var-label> strings may use 'suffix'
notation.  In the case of variables this allows the preset to target
specific 'mult' variable instances or ranges of instances.
When the network is a poly network using suffix notation
with a <proc-label> allows the target to particular
instances or ranges of instances.


Final Notes:

1. External network preset application requests that come from the control application
(e.g. caw::main()), or requests that occur from any processor that is not in the top level,
must be deferred until the end of the execution cycle when no processors are running.

Network preset application requests that occur from top level processors (processors running
in the outmost network can be applied directly because by definition the top level processors
run synchronously.

One way to handle this is to have a 'apply_preset' at the top level that takes
a preset label as input and applies it directly.

2. Maybe network presets should only be 'label' based and and processor instance
presets should only be 'value' based? Does this actually help anything?
Given that the system isn't currently limited in this way maybe it doesn't matter.

3. To Do:

- Processor instance presets have not been implemented.
- Preset application request deferrment has not been implemented.

Variable Modifcation Tracking
------------------------------



The only thread that can write a variable value is the thread executing the variables proc.



