var _ws        = null;
var _rootId    = "0";
var _nextEleId = 0;
var _focusId   = null;
var _focusVal  = null;
var _rootDivEle = null;
var _rootEle = null;

function set_app_title( suffix, className )
{
    var ele = document.getElementById('connectTitleId');
    if(ele != null)
    {
	ele.innerHTML = suffix
	ele.className = className
    }
    else
    {
	console.log("Ele. not found. Set title failed.")
    }
}


function dom_child_by_id( parentEle, child_id )
{
    var childrenL = parentEle.children
    for(var i=0; i<childrenL.length; i++)
    {
	if( childrenL[i].id == child_id )
	{
	    return childrenL[i];
	}
    }
    return null;
}

function dom_set_option_by_text( ele_id, text )
{
    var ele = dom_id_to_ele(ele_id);
    
    for (var i = 0; i < ele.options.length; i++)
    {    
	if (ele.options[i].text === text)
	{
            ele.selectedIndex = i;
            break;
	}
    }
}



function dom_set_number( ele_id, val )
{
    dom_id_to_ele(ele_id).value = val
}


//==============================================================================
function dom_id_to_ele( id )
{
    var ele = null;
    
    if( _rootEle == null )
    {
	console.log("fail dtoe: root null");
    }
    else
    {
	if( id == _rootId )
	    return _rootEle
	
	//ele =  _rootEle.getElementById(id);
	
	ele = document.getElementById(id)
	if( ele == null )
	{
	    console.log("fail dtoe:" + id + " " + typeof(id) );
	}
    }
    return ele
}

// { return document.getElementById(id); }

function dom_set_checkbox( ele_id, fl )
{ dom_id_to_ele(ele_id).checked = fl }

function dom_get_checkbox( ele_id )
{ return dom_id_to_ele(ele_id).checked }

function dom_create_ele( ele_type )
{
    ele = document.createElement(ele_type);
    return ele
}

//==============================================================================

function ui_error( msg )
{
    console.log("Error: " + msg )
}

function ui_send_value( ele, typeId, value )
{
    ws_send("value " + ele.id + " " + typeId + " : " + value)  
}

function ui_send_corrupt_state( ele )
{
    ws_send("corrupt " + ele.id)
}

function ui_send_bool_value(   ele, value ) { ui_send_value(ele,'b',value); }
function ui_send_int_value(    ele, value ) { ui_send_value(ele,'i',value); }
function ui_send_float_value(   ele, value ) { ui_send_value(ele,'d',value); }
function ui_send_string_value( ele, value ) { ui_send_value(ele,'s',value); }

function ui_send_click( ele )
{
    //console.log("click " + ele.id )
        
    ws_send("click " + ele.id )  
}

function ui_send_select( ele, selectedFl )
{
    let selected_value = selectedFl ? 1 : 0;
    ws_send("select " + ele.id  + " " + selected_value )
}



function ui_send_echo( ele )
{
    ws_send("echo " + ele.id ) 
}

function ui_print_children( eleId )
{
    var childrenL = dom_id_to_ele(eleId).children
    
    for(var i=0; i<childrenL.length; i++)
    {      
	console.log( childrenL[i] )
    }      
}

function ui_get_parent( parentId )
{
    if( parentId==null || parentId.trim().length == 0 )
	parentId = _rootId
    
    parent_ele = dom_id_to_ele(parentId);
    
    if( parent_ele == null )
    {
	ui_error("Parent element id: " + parentId + " not found.")
    }

    return parent_ele;  
}

function ui_on_click( ele, evt )
{
    ui_send_click(ele);
    evt.stopPropagation();
}

function ui_create_ele( parent_ele, ele_type, d, dfltClassName )
{
    // create the ctl object
    var ele = dom_create_ele(ele_type);

    if( ele == null )
	ui_error(ele_type +" element create failed.");
    else
    {
	ele.id = d.uuId;

	if(d.hasOwnProperty('className') )
	    ele.className = d.className;
	else
	    ele.className = dfltClassName;

	if(d.hasOwnProperty('addClassName') )
	{
	    ele.className += " " + d.addClassName
	}

	if(d.hasOwnProperty('appId'))
	    ele.appId = d.appId;
	else
	    ele.appId = null;

	if( d.hasOwnProperty('clickable') )
	    ui_set_clickable( ele, d.clickable );

	if( d.hasOwnProperty('enable') )
	    ui_set_enable( ele, d.enable )

	//console.log("Created: " + ele_type  + " parent:" + d.parentUuId + " id:" + ele.id + " appId:" + ele.appId)
	
	parent_ele.appendChild(ele);
	
	if( d.hasOwnProperty('order') )
	    ui_set_order_key(ele,d.order)
	
    }
    return ele
}

function ui_create_ctl( parent_ele, ele_type, label, d, dfltEleClassName )
{
    // create an enclosing div
    var div_ele = dom_create_ele("div");

    div_ele.className = "uiCtlDiv " + dfltEleClassName + "Div"

    parent_ele.appendChild( div_ele );
    
    var label_ele = div_ele
    
    // if label is not null then create an enclosing 'label' element
    if( label != null )
    {
	label = label.trim();

	if( label.length > 0)
	{
	    label_ele = dom_create_ele("label");

	    label_ele.innerHTML = label;
	    
	    div_ele.appendChild(label_ele)
	}
    }

    return ui_create_ele( div_ele, ele_type, d, dfltEleClassName );
}

function ui_create_div( parent_ele, d )
{
    var div_ele =  ui_create_ele( parent_ele, "div", d, "uiDiv" );

    if( div_ele != null )
    {
	
	if( d.title !=  null )
	{
	    var title = d.title.trim()

	    if( title.length > 0 )
	    {
		var p_ele = dom_create_ele("p")
	    
		p_ele.innerHTML = title
	
		div_ele.appendChild( p_ele )
	    }
	}
    }
    
    return div_ele;
}

function ui_create_panel_div( parent_ele, d )
{
    d.type = "div"

    if( !d.hasOwnProperty('className') )
	d.className = "uiPanel"
    
    var div_ele =  ui_create_div( parent_ele, d );

    

    return div_ele
}

function ui_create_row_div( parent_ele, d )
{
    d.type = "div"

    if( !d.hasOwnProperty('className') )
	d.className = "uiRow"
    
    var div_ele =  ui_create_div( parent_ele, d );


    return div_ele
}

function ui_create_col_div( parent_ele, d )
{
    d.type = "div"

    if( !d.hasOwnProperty('className') )
	d.className = "uiCol"
    
    var div_ele =  ui_create_div( parent_ele, d );


    return div_ele
}


function ui_create_label( parent_ele, d )
{
    var ele = ui_create_ele( parent_ele, "label", d, "uiLabel" );

    if( ele != null )
    {
	ele.innerHTML = d.title;
    }
    
    return ele;
}

function ui_create_button( parent_ele, d )
{
    var ele = ui_create_ctl( parent_ele, "button", null, d, "uiButton" );

    if( ele != null )
    {
	ele.innerHTML = d.title;
	ele.onclick   = function() { ui_send_int_value(this,1); }
    }
    
    return ele;
}

function ui_create_check( parent_ele, d )
{
    var ele = ui_create_ctl( parent_ele, "input", d.title, d, "uiCheck" )
    
    if( ele != null )
    {
	ele.type = "checkbox";

	ele.onclick = function() {  ui_send_bool_value(this,dom_get_checkbox(this.id)); }

	if( !d.hasOwnProperty('value') )
	{
	    ui_send_echo(ele)
	}
	else
	{
	    dom_set_checkbox(ele.id, d.value );
	    ui_send_bool_value(ele,dom_get_checkbox(ele.id))
	}

    }
    return ele;
}

//
// Note: The value of a 'select' widget is always set by the 'appId'
// of the selected 'option'.  Likewise the 'appId' of the selected
// option is returned as the value of the select widget.
//
function ui_on_select( ele )
{
    ui_send_int_value(ele,ele.options[ ele.selectedIndex ].appId);
}

function ui_select_set_from_option_app_id( sel_ele, option_appId )
{
    var i;
    for(i=0; i<sel_ele.options.length; ++i)
	if( sel_ele.options[i].appId == option_appId )
        {
	    sel_ele.selectedIndex = i;
	    return;
        }

    ui_error("Select option index not found.");    
}

function ui_create_select( parent_ele, d )
{
    var ele = ui_create_ctl( parent_ele, "select", d.title, d, "uiSelect" );
    ele.onchange = function() { ui_on_select(this) }

    if( !d.hasOwnProperty('value') )
    {
	ui_send_echo(ele)
    }
    else
    {
	// Note that d.value is the appId of the default selected option
	ele.defaultOptionAppId = d.value; 
	ui_send_int_value(ele,ele.defaultOptionAppId)
    }
    
    return ele;
}

function ui_create_option( parent_ele, d )
{
    var opt_ele = ui_create_ele( parent_ele, "option", d, "uiOption" );

    if( opt_ele != null )
    {
	if(d.hasOwnProperty('className'))
	    opt_ele.className = d.className;

	opt_ele.innerHTML = d.title;

	// d.value, if it exists, is a boolean indicating that this is the default option
	var fl0 = d.hasOwnProperty('value') && d.value != 0;

	// The parent 'select' element may also have been given the app id of the default option
	// (and this option may be it)
	var fl1 = parent_ele.hasOwnProperty('defaultOptionAppId') && parent_ele.defaultOptionAppId == ele.appId;
    
	if(fl0 || fl1 )
	{
	    parent_ele.selectedIndex = parent_ele.options.length-1;
	}
    }
    
    return opt_ele;
}

function _ui_on_focus( ele )
{
    _focusId  = ele.id;
    _focusVal = ele.value;
}


function ui_set_str_display( ele_id, value )
{
    
    var ele = dom_id_to_ele(ele_id);

    if( typeof(value)=="string")
    {
	ele.innerHTML = value;
    }
}

function ui_create_str_display( parent_ele, d )
{
    var ele = ui_create_ctl( parent_ele, "label", d.title, d, "uiStringDisp" );
    
    if( ele != null )
    {
	if( d.hasOwnProperty('value') )
	{
	    ui_set_str_display(ele.id, d.value);
	}
	else
	{
	    ui_send_echo(ele);
	}
    }
    
    return ele;
}

function _ui_on_string_blur( ele )
{
    if( ele.id == _focusId )
    {
	if( ele.value != _focusVal )
	    ui_send_string_value(ele,ele.value)
    }
}

function ui_create_string( parent_ele, d )
{
    var ele = ui_create_ctl( parent_ele, "input", d.title, d, "uiString" );

    if( ele != null )
    {
	ele.addEventListener('keyup', function(e) { if(e.keyCode===13){  ui_send_string_value(this, this.value); }} );
	ele.addEventListener('focus', function(e) { _ui_on_focus(this); } );
	ele.addEventListener('blur',  function(e) { _ui_on_string_blur(this); }  );

	if( !d.hasOwnProperty('value') )
	    ui_send_echo(ele);
	else
	{
	    ele.value  = d.value;
	    ui_send_string_value(ele,ele.value)
	}
	    
    }

    return ele;
}


function _ui_send_number( ele )
{
    var val = 0;
    if( ele.decpl == 0 )
	val = Number.parseInt(ele.value)
    else
	val = Number.parseFloat(ele.value)

    if( !(ele.minValue<=val && val<=ele.maxValue))
    {
	ele.style.borderColor = "red"
	ui_send_corrupt_state(ele)
    }
    else	
    {
	ele.style.borderColor = ""

	if( ele.decpl == 0 )
	    ui_send_int_value(ele,ele.value);
	else
	    ui_send_float_value(ele,ele.value);
    }   
}

function ui_number_keyup( e )
{
    if( e.keyCode===13 )
    {
	var ele = dom_id_to_ele(e.target.id)

	if( ele != null )
	{
	    //console.log("min:"+ele.minValue+" max:"+ele.maxValue)
	    _ui_send_number(ele)
	}
    }
}

function _ui_on_number_blur( ele )
{
    if( ele.id == _focusId )
    {
	if( ele.value != _focusVal )
	    _ui_send_number(ele)
    }
}

function _ui_set_number_range( ele, d )
{
    if(d.max < d.min)
    {
	ui_error("Numeric range max: " + d.maxValue + " is not greater than " + d.minValue + ".")
    }
    else
    {
	ele.maxValue  = d.max;
	ele.minValue  = d.min;
	ele.stepValue = d.step;
	ele.decpl     = d.decpl;
    }
}

function ui_set_number_value( ele, value )
{
    var min_ok_fl = (!ele.hasOwnProperty('minValue')) || (value >= ele.minValue)
    var max_ok_fl = (!ele.hasOwnProperty('maxValue')) || (value <= ele.maxValue)
    
    if( min_ok_fl && max_ok_fl    )
    {
	ele.value     = value;
	if( ele.decpl == 0 )
	    ui_send_int_value( ele, ele.value )
	else
	    ui_send_float_value( ele, ele.value )
    }
    else
    {
	ui_error("Number value " + value + " out of range. min:" + ele.minValue + " max:" +ele.maxValue )
    }

}

function ui_set_number_range( ele, d )
{
    _ui_set_number_range(ele,d)
    if( d.hasOwnProperty('value') )
	ui_set_number_value(ele,d.value)
}

function ui_create_number( parent_ele, d )
{
    var ele = ui_create_ctl( parent_ele, "input", d.title, d, "uiNumber" );
    
    if( ele != null )
    {
	ele.addEventListener('keyup', ui_number_keyup );
	ele.addEventListener('focus', function(e) { _ui_on_focus(this); } );
	ele.addEventListener('blur',  function(e) { _ui_on_number_blur(this); }  );
	_ui_set_number_range(ele,d)
	

	if( d.hasOwnProperty('value') )
	{
	    ui_set_number_value(ele,d.value)
	}
	else
	{
	    ui_send_echo(ele);
	}
    }
    return ele;
}

function ui_set_number_display( ele_id, value )
{
    //console.log("Numb disp: " + ele_id + " " + value)
    
    var ele = dom_id_to_ele(ele_id);

    if( typeof(value)=="number")
    {
	var val = value.toString();
    
	if( ele.decpl == 0 )
	    ele.innerHTML = parseInt(val,10);
	else
	    ele.innerHTML = parseFloat(val);
    }
}

function ui_create_number_display( parent_ele, d )
{
    var ele = ui_create_ctl( parent_ele, "label", d.title, d, "uiNumbDisp" );
    
    if( ele != null )
    {
	ele.decpl = d.decpl;

	if( d.hasOwnProperty('value') )
	{
	    ui_set_number_display(ele.id, d.value);
	}
	else
	{
	    ui_send_echo(ele);
	}
    }
    
    return ele;

}

function ui_create_text_display( parent_ele, d )
{
    return ui_create_ctl( parent_ele, "label", d.title, d, "uiTextDisp" );
}


function ui_set_progress( ele, value )
{
    ele.value = Math.round( ele.max * (value - ele.minValue) / (ele.maxValue - ele.minValue));
}

function _ui_set_prog_range( ele, d )
{
    ele.maxValue = d.max;
    ele.minValue = d.min;    
}

function ui_create_progress( parent_ele, d )
{
    var ele = ui_create_ctl( parent_ele, "progress", d.title, d, "uiProgress" );

    if( ele != null )
    {
	ele.max      = 100;
	_ui_set_prog_range(ele,d)
	
	if( !d.hasOwnProperty('value') )
	    ui_send_echo(ele);
	else
	{
	    ui_set_progress( ele, d.value );
	    ui_send_int_value( ele, ele.value );
	}
 
    }
    return ele
}

function ui_set_prog_range( ele, d )
{
    _ui_set_prog_range(ele,d)
    if( d.hasOwnProperty('value'))
	ui_set_progress(ele,d.value)	
}

function _on_log_click( evt )
{
    var pre_ele = dom_id_to_ele(evt.target.id)

    pre_ele.auto_scroll_flag = !pre_ele.auto_scroll_flag;
}

function ui_set_log_text( ele, value )
{
    var child_id = ele.id + "_pre"

    for(var i=0; i<ele.children.length; ++i)
    {
	var pre_ele = ele.children[i]
	if( pre_ele.id == child_id )
	{
	    pre_ele.innerHTML += value

	    if(pre_ele.auto_scroll_flag)		
		ele.scrollTop = pre_ele.clientHeight
	    
	    break;
	}
    }    
}

function ui_create_log( parent_ele, d )
{
    // create a containing div with the label
    
    if( !d.hasOwnProperty('className') )
	d.className = "uiLog"
    
    var log_ele  = ui_create_ctl( parent_ele, "div", d.title, d, "uiLog" )

    

    // add a <pre> to the containing div
    var ele = dom_create_ele("pre")
    
    ele.id      = log_ele.id + "_pre"  
    ele.onclick = _on_log_click;
    ele.auto_scroll_flag = true;
    
    log_ele.appendChild(ele)

    return log_ele
}

function ui_create_list( parent_ele, d )
{
    //console.log(d)
    var list_ele  = ui_create_ctl( parent_ele, "div", d.title, d, "uiList" )
    
    return list_ele
}

function ui_set_value( d )
{
    var eleId = d.uuId.toString()
    var ele = dom_id_to_ele(eleId)

    if( ele == null )
	console.log("ele '"+eleId+"' not found");
    else
    {
	if( !ele.hasOwnProperty("uiEleType") )
        {
	    console.log("No type");
        }
    }
    
    if( ele != null && ele.hasOwnProperty("uiEleType"))
    {
	//console.log("found: "+ele.uiEleType)
	
	switch( ele.uiEleType )
	{
	    case "div":
	    break;

	    case "label":
	    ele.innerHTML = d.value
	    break;

	    case "button":	    
	    break;

	    case "check":
	    dom_set_checkbox(ele.id,d.value)
	    break;

	    case "select":
	    ui_select_set_from_option_app_id(ele,d.value)
	    break;

	    case "option":
	    break;

	    case "str_disp":
	    ui_set_str_display(ele.id,d.value);
	    break

	    case "string":
	    ele.value = d.value
	    break;

	    case "number":
	    ele.value = d.value
	    break;

	    case "numb_disp":
	    ui_set_number_display(ele.id,d.value);
	    break;
	    
	    case "progress":
	    ui_set_progress( ele, d.value )
	    //ele.value = d.value
	    break;

	    case "log":
	    ui_set_log_text( ele, d.value )
	    break
	    
	    default:
	    ui_error("Unknown UI element type on set value: " + d.type )
	}
    }
}

function _ui_modify_class( ele, classLabelArg, enableFl )
{
    let classLabel  = " " + classLabelArg; // prefix the class label with a space

    //console.log(ele.id + " " + classLabelArg + " " + enableFl )

    let isEnabledFl = false;
    
    if( ele.hasOwnProperty("className") )
	isEnabledFl = ele.className.includes(classLabel)
    else
	ele.className = ""

    // if the class is not already enabled/disabled
    if( enableFl != isEnabledFl )
    {
	if( enableFl )
	    ele.className += classLabel;
	else
	    ele.className = ele.className.replace(classLabel, "");
    }

    //console.log(ele.id + " " + ele.className + " " + enableFl )
}

function ui_set_select( ele, enableFl )
{
    _ui_modify_class(ele,"uiSelected",enableFl)
    ui_send_select( ele, enableFl )
}


function ui_set_clickable( ele, enableFl )
{
    ele.clickableFl = enableFl
    
    if(enableFl)
	ele.onclick = function( evt ){ ui_on_click( this, evt ); }
    else
	ele.onclick = null
}

function ui_set_visible( ele, enableFl )
{    
    if(enableFl)
    {
	if(ele.hasOwnProperty("style_display") )
	{
	    ele.style.display = ele.style_display;
	}
	else
	{
	    ele.style.display = "block";
	}
    }
    else
    {
	ele.style_display = ele.style.display;
	ele.style.display = "none";
    }
}

function ui_set_enable( ele, enableFl )
{
    ele.disabled = !enableFl
}

function ui_set_order_key(ele, orderKey)
{
    let parent  = ele.parentElement // get the parent of the element to reorder    
    ele = parent.removeChild( ele ) // remove the element to reorder from the parent list
    
    ele.order = orderKey

    let i = 0;
    for(i=0; i<parent.children.length; ++i)
    {
	if( parent.children[i].hasOwnProperty("order") && parent.children[i].order >= orderKey)
	{
	    parent.insertBefore( ele, parent.children[i] )
	    break
	}
    }

    // no element was found greater than this element  ....
    if( i == parent.children.length )
	parent.appendChild(ele) // ... insert the element at the end of the child lsit
    
}

function ui_set( d )
{
    //console.log(d)
    var ele = dom_id_to_ele(d.uuId.toString())

    if( ele == null )
	console.log("ele not found");
    
    if( ele != null)
    {
	switch( d.type )
	{
	    case "number_range":
	    ui_set_number_range(ele, d)
	    break;

	    case "progress_range":
	    ui_set_prog_range(ele, d)
	    break;

	    case "select":
	    ui_set_select(ele,d.value)
	    break

	    case "clickable":
	    ui_set_clickable(ele,d.value)
	    break

	    case "visible":
	    ui_set_visible(ele,d.value)
	    break

	    case "enable":
	    ui_set_enable(ele,d.value)
	    break

	    case "order":
	    ui_set_order_key(ele,d.value)
	    break
	    
	}
    }
}

function ui_cache( d )
{
    for(i=0; i<d.array.length; ++i)
    {
	_ws_on_msg( d.array[i] )
    }
}

function ui_create( d )
{
    if( typeof(d.parentUuId) == "number")
	d.parentUuId = d.parentUuId.toString()
    
    if( typeof(d.uuId) == "number" )
	d.uuId = d.uuId.toString()
    
    
    var parent_ele  = ui_get_parent(d.parentUuId);
    var ele = null;
    
    if( parent_ele == null )
	console.log("Parent ele not found.",d)
    else
    {    
	switch( d.type )
	{
	    case "div":
	    ele = ui_create_div( parent_ele, d )
	    break;

	    case "panel":
	    ele = ui_create_panel_div( parent_ele, d )
	    break;

	    case "row":
	    ele = ui_create_row_div( parent_ele, d )
	    break;

	    case "col":
	    ele = ui_create_col_div( parent_ele, d )
	    break;

	    case "label":
	    ele = ui_create_label( parent_ele, d )
	    break;

	    case "button":
	    ele = ui_create_button( parent_ele, d )
	    break;

	    case "check":
	    ele = ui_create_check( parent_ele, d )
	    break;

	    case "select":
	    ele = ui_create_select( parent_ele, d );
	    break;

	    case "option":
	    ele = ui_create_option( parent_ele, d );
	    break;

	    case "str_disp":
	    ele = ui_create_str_display( parent_ele, d );
	    break;
	    
	    case "string":
	    ele = ui_create_string( parent_ele, d );
	    break;

	    case "number":
	    ele = ui_create_number( parent_ele, d );
	    break;
	    
	    case "numb_disp":
	    ele = ui_create_number_display( parent_ele, d );
	    break;

	    case "text_disp":
	    ele = ui_create_text_display( parent_ele, d );
	    break;	    

	    case "progress":
	    ele = ui_create_progress( parent_ele, d );
	    break;

	    case "log":
	    ele = ui_create_log( parent_ele, d );
	    break;

	    case "list":
	    ele = ui_create_list( parent_ele, d );
	    break;
	    
	    default:
	    ui_error("Unknown UI element type: " + d.type )
	}

	if( ele != null )
	{
	    ele.uiEleType = d.type;
	}

    }
}

function ui_destroy( d )
{
    if( typeof(d.uuId) == "number" )
	d.uuId = d.uuId.toString()
    
    var ele = dom_id_to_ele(d.uuId)

    if( ele != null )
	ele.parentElement.removeChild( ele )
}

function ui_attach( d )
{
    console.log("ATTACH");
    //_rootDivEle.appendChild(_rootEle)
}


function ws_send( s )
{
    //console.log(s)
    
    _ws.send(s+"\0")
}

function _ws_on_msg( d )
{
    switch( d.op )
    {
	case 'cache':
	ui_cache( d )
	break;

	case 'create':
	ui_create( d )
	break;

	case 'destroy':
	ui_destroy( d )
	break

	case 'value':
	ui_set_value( d )
	break;

	case 'set':
	ui_set( d )
	break;

	case 'attach':
	ui_attach(d)
	break;
	
	default:
	ui_error("Unknown UI operation. " + d.op )
    }

}

function ws_on_msg( jsonMsg )
{
    //console.log(jsonMsg)
    
    d = JSON.parse(jsonMsg.data);

    _ws_on_msg(d)
}

function ws_on_open()
{
    set_app_title( "Connected", "title_connected" );
    ws_send("init")
}

function ws_on_close()
{
    set_app_title( "Disconnected", "title_disconnected" );

    // remove the body of UI
    var rootEle = dom_id_to_ele(_rootId)
    if( rootEle != null )
      document.body.removeChild(  rootEle )
}

function ws_form_url(urlSuffix)
{
    var pcol;
    var u = document.URL;

    pcol = "ws://";
    if (u.substring(0, 4) === "http")
	u = u.substr(7);

    u = u.split("/");

    return pcol + u[0] + "/" + urlSuffix;
}

function main_0()
{
    d = { "className":"uiAppDiv", "uuId":_rootId }
    rootEle = ui_create_div( document.body, d )
    rootEle.uuId = 0;
    rootEle.id = _nextEleId;
    _nextEleId += 1;

    //console.log(ws_form_url(""))
    
    _ws = new WebSocket(ws_form_url(""),"ui_protocol")
    
    _ws.onmessage    = ws_on_msg
    _ws.onopen       = ws_on_open 
    _ws.onclose      = ws_on_close;
}

function main()
{
    d = { "className":"uiAppDiv", "uuId":"rootDivEleId" }
    _rootDivEle = ui_create_div( document.body, d )

    //_rootEle = document.createDocumentFragment();
    _rootEle = _rootDivEle
    
    _rootEle.uuId = 0;
    _rootEle.id = _nextEleId;
    _nextEleId += 1;

    //console.log(ws_form_url(""))
    
    _ws = new WebSocket(ws_form_url(""),"ui_protocol")
    
    _ws.onmessage    = ws_on_msg
    _ws.onopen       = ws_on_open 
    _ws.onclose      = ws_on_close;

    console.log("main() done.")
}

