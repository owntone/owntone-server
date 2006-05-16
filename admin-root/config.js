Event.observe(window,'load',function (e) {Config.init();});

// Config isn't defined until after the Event.observe above
// I could have put it below Config = ... but I want all window.load events
// at the start of the file
function init() {
  Config.init();
}
function hej() {
  alert(Form.serialize('theform'));
    
}
var ConfigXML = {
  config: [],
  getOption: function (section,id) {
    return this.config[section][id];
  },
  getSections: function () {
    return $H(this.config).keys();
  },
  getItems: function(section) {
    return $H(this.config[section]).keys();
  },
  parseXML: function(xmlDoc) {
    $A(xmlDoc.getElementsByTagName('section')).each(function (section) {
      var items = {};
      var option;
      $A(section.getElementsByTagName('item')).each(function (item) {
        option = {config_section: item.getAttribute('config_section'),
                            name: Element.textContent(item.getElementsByTagName('name')[0]),
               short_description: Element.textContent(item.getElementsByTagName('short_description')[0]),
                            type: Element.textContent(item.getElementsByTagName('type')[0])};
        if ('select' == option.type) {
          var options = [];
          $A(item.getElementsByTagName('option')).each(function (option) {
            options.push({value: option.getAttribute('value'),
                          label: Element.textContent(option)});
          });
          option.options = options;
          option.default_value = Element.textContent(item.getElementsByTagName('default_value')[0]);
        }
        if (('short_text_multiple' == option.type) ||
            ('long_text_multiple' == option.type)) {
          option.add_item_text = Element.textContent(item.getElementsByTagName('add_item_text')[0]);  
        }
        items[item.getAttribute('id')] = option;
      });
      ConfigXML.config[section.getAttribute('name')] = items;
    });
  }
};
var Config = {
  configPath: '',
  configOptionValues: '',
  init: function () {
    new Ajax.Request('/config.xml',{method: 'get',onComplete: Config.storeConfigLayout});
  },
  storeConfigLayout: function (request) {
    Config.layout = request.responseXML;
    ConfigXML.parseXML(request.responseXML);
    new Ajax.Request('/xml-rpc?method=stats',{method: 'get',onComplete: Config.updateStatus});
  },
  updateStatus: function (request) {
    Config.configPath = Element.textContent(request.responseXML.getElementsByTagName('config_path')[0]);
//    $('config_path').appendChild(document.createTextNode(
      
//    );
    new Ajax.Request('/xml-rpc?method=config',{method: 'get',onComplete: Config.showConfig});  
  },
  showConfig: function (request) {
    Config.configOptionValues = request.responseXML;
    var sections = ConfigXML.getSections();
    sections.each(function (section) {
      var head = document.createElement('div');
      head.className= 'naviheader';
      head.appendChild(document.createTextNode(section));
      var body = document.createElement('div');
      body.className = 'navibox';
      if ('Server' == section) {
        body.appendChild(Builder.node('span',{id:'config_path'},'Config File'));
        body.appendChild(document.createTextNode(Config.configPath));
        body.appendChild(Builder.node('br'));
        body.appendChild(Builder.node('div',{style: 'clear: both;'}));
      }
      ConfigXML.getItems(section).each(function (itemId) {
        body.appendChild(Config._buildItem(section,itemId));
      });
      $('theform').appendChild(head);
      $('theform').appendChild(body);
    });  
  },
  _getConfigOptionValue: function(id,multiple) {
    if (multiple) {
      var ret = [];
      var option = Config.configOptionValues.getElementsByTagName(id);
      if (option.length > 0) { 
        $A(option[0].getElementsByTagName('item')).each(function (item) {
          ret.push(Element.textContent(item));
        });
      } else {
        ret.push('');
      }
      return ret;
    } else {
      var value = Config.configOptionValues.getElementsByTagName(id);
      if (value.length > 0) {
        return Element.textContent(value[0]);
      } else {
        return '';
      }
    }
  },
  _buildItem: function(section,itemId) {
    var frag = document.createDocumentFragment();
    var href;
    var span;
    var item = ConfigXML.getOption(section,itemId);
    var postId = item.config_section + ':' + itemId;
    var inputSize = 80;
    var noBrowse = false;
    switch(item.type) {
      case 'short_text':
        inputSize = 20;
        // Yes, we're falling through
      case 'long_text':
        frag.appendChild(BuildElement.input(postId,
                                 item.name,
                                 Config._getConfigOptionValue(itemId),
                                 inputSize,
                                 item.short_description,                                     
                                 ''));
        frag.appendChild(Builder.node('br'));
        break;
      case 'short_text_multiple':
        inputSize = 20;
        noBrowse = true;
        // Yes, we're falling through
      case 'long_text_multiple':
        Config._getConfigOptionValue(itemId,true).each(function (value,i) {
          var span = document.createElement('span');
          span.appendChild(BuildElement.input(postId+i,
                                             item.name,
                                             value,inputSize,
                                             item.short_description
          ));
          if (!noBrowse) {
            href = Builder.node('a',{href: 'javascript://'},'Browse');
            Event.observe(href,'click',Config._browse);          
            span.appendChild(href);
          }
          span.appendChild(document.createTextNode('\u00a0\u00a0'));
          href = Builder.node('a',{href: 'javascript://'},'Remove');
          Event.observe(href,'click',Config._removeItem);
          span.appendChild(href);
          span.appendChild(Builder.node('br'));
          frag.appendChild(span);                                             
        });
        href = Builder.node('a',{href:'javascript://',className:'addItemHref'},
                                     item.add_item_text);
        frag.appendChild(href);
        Event.observe(href,'click',Config._addItem);
        frag.appendChild(Builder.node('div',{style:'clear: both'}));
        break;                                  
      case 'select':
        var value = Config._getConfigOptionValue(itemId);
        if (!value) {
          value = item.default_value;
        }
        frag.appendChild(BuildElement.select(postId,
                                  item.name,
                                  item.options,
                                  value,
                                  item.short_description));
        frag.appendChild(Builder.node('br'));
        break;
    }
    return frag;
  },
  _addItem: function(e) {
    var span = Event.element(e);
    while (span.nodeName.toLowerCase() != 'span') {
      span = span.previousSibling;
    }
    var frag = document.createDocumentFragment();
    span = span.cloneNode(true);
    span.getElementsByTagName('label')[0].setAttribute('for','hej');
    span.getElementsByTagName('input')[0].id = 'hej';
    span.getElementsByTagName('input')[0].value = '';
    var hrefs = span.getElementsByTagName('a');
    if ('Netscape' == navigator.appName) {
      // Firefox et al doesn't copy registered events on an element deep clone
      // Don't know if that is w3c or if IE has it right
      if (hrefs.length == 1) {
        Event.observe(hrefs[0],'click',Config._removeItem);
      } else {
        Event.observe(hrefs[0],'click',Config._browse);          
        Event.observe(hrefs[1],'click',Config._removeItem);        
      }
    }
    var src = Event.element(e);
    src.parentNode.insertBefore(span,src);
  },
  _removeItem: function(e) {
    var span = Event.element(e);
    while (span.nodeName.toLowerCase() != 'span') {
      span = span.parentNode;
    }
    if ((span.previousSibling && span.previousSibling.nodeName.toLowerCase() == 'span')||
        (span.nextSibling.nodeName.toLowerCase() == 'span'))  {
      Element.remove(span);
    } else {
      span.getElementsByTagName('input')[0].value='';
    }
  },
  _browse: function(e) {
    alert('Browse directories');
  }
}
var BuildElement = {
  input: function(id,displayName,value,size,short_description,long_description) {

    var frag = document.createDocumentFragment();
    var label = document.createElement('label');

    label.setAttribute('for',id);
    label.appendChild(document.createTextNode(displayName));
    frag.appendChild(label);

    frag.appendChild(Builder.node('input',{id: id,name: id,className: 'text',
                                           value: value,size: size}));
    frag.appendChild(document.createTextNode('\u00a0'));                                              
    frag.appendChild(document.createTextNode(short_description));

    return frag;
  },
  select: function(id,displayName,options,value,short_description,long_description) {
    var frag = document.createDocumentFragment();
    var label = document.createElement('label');
    label.setAttribute('for',id);
    label.appendChild(document.createTextNode(displayName));
    frag.appendChild(label);
    
    var select = Builder.node('select',{id: id,name: id,size: 1});
    $A(options).each(function (option) {
      select.appendChild(Builder.node('option',{value: option.value},
                                                option.label));
    });
    select.value = value;
    frag.appendChild(select);
    frag.appendChild(document.createTextNode('\u00a0'));                                              
    frag.appendChild(document.createTextNode(short_description));

    return frag;
  }
    
}
Object.extend(Element, {
  removeChildren: function(element) {
  while(element.hasChildNodes()) {
    element.removeChild(element.firstChild);
  }
  },
  textContent: function(node) {
  if ((!node) || !node.hasChildNodes()) {
    // Empty text node
    return '';
  } else {
    if (node.textContent) {
    // W3C ?
    return node.textContent;
    } else if (node.text) {
    // IE
    return node.text;
    }
  }
  // We shouldn't end up here;
  return '';
  }
});
/* Detta script finns att hamta pa http://www.jojoxx.net och 
   far anvandas fritt sa lange som dessa rader star kvar. */ 
function DataDumper(obj,n,prefix){
	var str=""; prefix=(prefix)?prefix:""; n=(n)?n+1:1; var ind=""; for(var i=0;i<n;i++){ ind+="  "; }
	if(typeof(obj)=="string"){
		str+=ind+prefix+"String:\""+obj+"\"\n";
	} else if(typeof(obj)=="number"){
		str+=ind+prefix+"Number:"+obj+"\n";
	} else if(typeof(obj)=="function"){
		str+=ind+prefix+"Function:"+obj+"\n";
	} else if(typeof(obj) == 'boolean') {
       str+=ind+prefix+"Boolean:" + obj + "\n";
	} else {
		var type="Array";
		for(var i in obj){ type=(type=="Array"&&i==parseInt(i))?"Array":"Object"; }
		str+=ind+prefix+type+"[\n";
		if(type=="Array"){
			for(var i in obj){ str+=DataDumper(obj[i],n,i+"=>"); }
		} else {
			for(var i in obj){ str+=DataDumper(obj[i],n,i+"=>"); }
		}
		str+=ind+"]\n";
	}
	return str;
}
