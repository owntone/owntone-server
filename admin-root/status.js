Event.observe(window,'load',initStatus);

var UPDATE_FREQUENCY = 5000; // number of ms between updates

function initStatus(e) {
  new Ajax.Request('xml-rpc?method=stats&iefix='+Math.random(),{method: 'get',onComplete:rsStats});

  window.setInterval(function () {
    new Ajax.Request('xml-rpc?method=stats&iefix='+Math.random(),{method: 'get',onComplete:rsStats});
  },UPDATE_FREQUENCY);

}
function rsStats(request) {
  ['service','stat'].each(function (tag) {
    $A(request.responseXML.getElementsByTagName(tag)).each(function (element) {
      var node = $(element.firstChild.firstChild.nodeValue.toLowerCase().replace(/\ /,'_'));  
      node.replaceChild(document.createTextNode(element.childNodes[1].firstChild.nodeValue),node.firstChild);
    });
  });  
  var thread = $A(request.responseXML.getElementsByTagName('thread'));
  var threadTable = new Table('thread');
  threadTable.removeTBodyRows();
  thread.each(function (element) {
    row = [];
    row.push(element.childNodes[1].firstChild.nodeValue);
    row.push(element.childNodes[2].firstChild.nodeValue);
    threadTable.addTbodyRow(row);    
  });
}

Table = Class.create();
Object.extend(Table.prototype,{
  initialize: function (id) {
    this.tbody = $(id).getElementsByTagName('tbody')[0];  
  },
  removeTBodyRows: function () {
    Element.removeChildren(this.tbody);
  },
  addTbodyRow: function (cells) {
    var tr = document.createElement('tr');
    cells.each(function (cell) {
      var td = document.createElement('td');
      td.appendChild(document.createTextNode(cell));
      tr.appendChild(td);
    });
    this.tbody.appendChild(tr);
  }
});
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