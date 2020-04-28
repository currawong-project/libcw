var _ws = null;
var _rootId = "0";
var _nextEleId   = 0;

function set_app_title( suffix, className )
{
    var ele = document.getElementById('appTitleId');
    ele.innerHTML = "UI Test:" + suffix
    ele.className = className
}


function uiOnError( msg, r)
{
    console.log("Error:" + msg);
}

function uiGetParent( r )
{
    parent_ele = document.getElementById(r.parent_id);
    
    if( parent_ele == null )
    {
	uiOnError("Parent not found. parent_id:" + r.parent_id,r);
    }

    return parent_ele;
}

function uiCreateEle( r )
{
    var parent_ele;
    
    if((parent_ele = uiGetParent(r)) != null )
    {    
	ele            = document.createElement(r.ele_type)
	ele.id         = r.ele_id;
	ele.className  = r.value;
	
	parent_ele.appendChild(ele)
    }
}

function uiRemoveChildren( r )
{
    ele = document.getElementById(r.ele_id)
    
    while (ele.firstChild)
    {
	ele.removeChild(ele.firstChild);
    }  
}

function uiDivCreate( r )
{ uiCreateEle(r) }

function uiLabelCreate( r )
{
    var parent_ele;
    
    if((parent_ele = uiGetParent(r)) != null )
    {
	ele            = document.createElement("label")
	ele.htmlFor    = r.ele_id
	ele.innerHTML  = r.value;
	parent_ele.appendChild(ele)
    }
    
}

function uiSelectCreate( r )
{
    uiCreateEle(r)
}

function uiSelectClear( r )
{ uiRemoveChildren(r) }

function uiSelectInsert( r )
{
    var select_ele;
    
    if((select_ele = uiGetParent(r)) != null )    
    {
	var option    = document.createElement('option');

	option.id        = r.ele_id;
	option.innerHTML = r.value;
	option.value     = r.ele_id;
	option.onclick   = function() { uiOnSelectClick(this) }
	
	select_ele.appendChild(option)
    }
}

function uiSelectChoose( r )
{
    var select_ele;
    
    if((select_ele = uiGetParent(r)) != null )    
    {
	if( select_ele.hasChildNodes())
	{
	    var children = select_ele.childNodes
	    for(var i=0; i<children.length; i++)
	    {
		if( children[i].id == r.ele_id )
		{
		    select_ele.selectedIndex = i
		    break;
		}	
	    }
	}
    }
}

function uiOnSelectClick( ele )
{
    cmdstr = "mode ui ele_type select op choose parent_id "+ele.parentElement.id+" option_id " + ele.id
    websocket.send(cmdstr);

}

function uiNumberOnKeyUp( e )
{
    if( e.keyCode == 13 )
    {
	//console.log(e)
	cmdstr = "mode ui ele_type number op change parent_id "+e.srcElement.parentElement.id+" ele_id " + e.srcElement.id + " value " + e.srcElement.value    
	websocket.send(cmdstr);
    }
}


function uiNumberCreate( r )
{
    var parent_ele;
    
    if((parent_ele = uiGetParent(r)) != null )        
    {
	ele    = document.createElement("input")
	ele.id = r.ele_id
	ele.setAttribute('type','number')
	ele.addEventListener('keyup',uiNumberOnKeyUp)
	parent_ele.appendChild(ele)
    }
}

function uiNumberSet( r )
{
    var ele;

    //console.log("ele_id:" + r.ele_id + " parent_id:" + r.parent_id + " value:" + r.value)
    
    if((ele = document.getElementById(r.parent_id)) != null)
    {
	switch( r.ele_id )
	{
	    case "0":  ele.min   = r.value; break;
	    case "1":  ele.max   = r.value; break;
	    case "2":  ele.step  = r.value; break;
	    case "3":  ele.value = r.value; break;
	}
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
{ return document.getElementById(id); }

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

function ui_send_bool_value(   ele, value ) { ui_send_value(ele,'b',value); }
function ui_send_int_value(    ele, value ) { ui_send_value(ele,'i',value); }
function ui_send_float_value(   ele, value ) { ui_send_value(ele,'f',value); }
function ui_send_string_value( ele, value ) { ui_send_value(ele,'s',value); }

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

	if(d.hasOwnProperty('appId'))
	    ele.appId = d.appId;
	else
	    ele.appId = null;

	//console.log("Created: " + ele_type  + " parent:" + d.parentUuId + " id:" + ele.id + " appId:" + ele.appId)
	
	parent_ele.appendChild(ele);

	
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
	label_ele = dom_create_ele("label");

	label_ele.innerHTML = label;

	div_ele.appendChild(label_ele)    
    }

    return ui_create_ele( div_ele, ele_type, d, dfltEleClassName );
}

function ui_create_div( parent_ele, d )
{
    var div_ele =  ui_create_ele( parent_ele, "div", d, "uiDiv" );

    if( div_ele != null )
    {
	
	if( d.title != null && d.title.length > 0 )
	{
	    var p_ele = dom_create_ele("p")
	    
	    p_ele.innerHTML = d.title
	
	    div_ele.appendChild( p_ele )
	}
    }
    
    return div_ele;
}

function ui_create_panel_div( parent_ele, d )
{
    d.type = "div"
    var div_ele =  ui_create_div( parent_ele, d );

    if( !d.hasOwnProperty('className') )
	div_ele.className = "uiPanel"

    return div_ele
}

function ui_create_row_div( parent_ele, d )
{
    d.type = "div"
    var div_ele =  ui_create_div( parent_ele, d );

    if( !d.hasOwnProperty('className') )
	div_ele.className = "uiRow"

    return div_ele
}

function ui_create_col_div( parent_ele, d )
{
    d.type = "div"
    var div_ele =  ui_create_div( parent_ele, d );

    if( !d.hasOwnProperty('className') )
	div_ele.className = "uiCol"

    return div_ele
}


function ui_create_title( parent_ele, d )
{
    return ui_create_ele( parent_ele, "label", d, "uiTitle" );
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
	    ui_send_echo(ele)
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

function ui_create_string( parent_ele, d )
{
    var ele = ui_create_ctl( parent_ele, "input", d.title, d, "uiString" );

    if( ele != null )
    {
	ele.addEventListener('keyup', function(e) { if(e.keyCode===13){  ui_send_string_value(this, this.value); }} );

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

function ui_number_keyup( e )
{
    if( e.keyCode===13 )
    {
	var ele = dom_id_to_ele(e.target.id)

	if( ele != null )
	{
	    //console.log("min:"+ele.minValue+" max:"+ele.maxValue)

	    var val = 0;
	    if( ele.decpl == 0 )
		val = Number.parseInt(ele.value)
	    else
		val = Number.parseFloat(ele.value)

	    if( !(ele.minValue<=val && val<=ele.maxValue))
		ele.style.borderColor = "red"
	    else	
	    {
		ele.style.borderColor = ""

		if( ele.decpl == 0 )
		    ui_send_int_value(ele,ele.value);
		else
		    ui_send_float_value(ele,ele.value);
	    }
	}
    }
}

function ui_create_number( parent_ele, d )
{
    var ele = ui_create_ctl( parent_ele, "input", d.title, d, "uiNumber" );
    
    if( ele != null )
    {
	ele.maxValue  = d.max;
	ele.minValue  = d.min;
	ele.stepValue = d.step;
	ele.decpl     = d.decpl;
	ele.addEventListener('keyup', ui_number_keyup );

	if( d.hasOwnProperty('value') && d.min <= d.value && d.value <= d.max )
	{
	    
	    ele.value     = d.value;
	    if( d.decpl == 0 )
		ui_send_int_value( ele, ele.value )
	    else
		ui_send_float_value( ele, ele.value )
	}
	else
	{
	    ui_send_echo(ele);
	}
    }
    return ele;
}

function ui_set_progress( ele_id, value )
{
    var ele = dom_id_to_ele(ele_id);

    ele.value = Math.round( ele.max * (value - ele.minValue) / (ele.maxValue - ele.minValue));
}

function ui_create_progress( parent_ele, d )
{
    var ele = ui_create_ctl( parent_ele, "progress", d.title, d, "uiProgress" );

    if( ele != null )
    {
	ele.max      = 100;
	ele.maxValue = d.max;
	ele.minValue = d.min;
	if( !d.hasOwnProperty('value') )
	    ui_send_echo(ele);
	else
	{
	    ui_set_progress( ele.id, d.value );
	    ui_send_int_value( ele, ele.value );
	}
 
    }
    return ele
}

function ui_set_value( d )
{
    //console.log(d)
    var ele = dom_id_to_ele(d.uuId.toString())

    if( ele == null )
	console.log("ele not found");
    else
	if( !ele.hasOwnProperty("uiEleType") )
	    console.log("No type");
    
    if( ele != null && ele.hasOwnProperty("uiEleType"))
    {
	//console.log("found: "+ele.uiEleType)
	
	switch( ele.uiEleType )
	{
	    case "div":
	    break;

	    case "title":
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

	    case "string":
	    ele.value = d.value
	    break;

	    case "number":
	    ele.value = d.value
	    break;

	    case "progress":
	    ele.value = d.value
	    break;
	    
	    default:
	    ui_error("Unknown UI element type: " + d.type )
	}
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
    
    if( parent_ele != null )
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

	    case "title":
	    ele = ui_create_title( parent_ele, d )
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

	    case "string":
	    ele = ui_create_string( parent_ele, d );
	    break;

	    case "number":
	    ele = ui_create_number( parent_ele, d );
	    break;

	    case "progress":
	    ele = ui_create_progress( parent_ele, d );
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




function ws_send( s )
{
    //console.log(s)
    
    _ws.send(s+"\0")
}

function ws_on_msg( jsonMsg )
{
    //console.log(jsonMsg)
    
    d = JSON.parse(jsonMsg.data);

    switch( d.op )
    {
	case 'create':
	ui_create( d )
	break;

	case 'value':
	ui_set_value( d )
	break;
	
	default:
	ui_error("Unknown UI operation. " + d.op )
    }

}

function ws_on_open()
{
    set_app_title( "Connected", "title_connected" );
    _ws.send("init")
}

function ws_on_close()
{
    set_app_title( "Disconnected", "title_disconnected" );
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

function main()
{
    rootEle = dom_id_to_ele(_rootId);
    rootEle.uuId = 0;
    rootEle.id = _nextEleId;
    _nextEleId += 1;

    //console.log(ws_form_url(""))
    
    _ws = new WebSocket(ws_form_url(""),"ui_protocol")
    
    _ws.onmessage    = ws_on_msg
    _ws.onopen       = ws_on_open 
    _ws.onclose      = ws_on_close;

    
}

