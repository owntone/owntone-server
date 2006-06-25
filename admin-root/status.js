Event.observe(window,'load',initStatus);

var UPDATE_FREQUENCY = 5000; // number of milliseconds between page updates

function initStatus(e) {
  Updater.start();
}
var Updater = {
  start: function () {
    window.setTimeout(Updater.update,UPDATE_FREQUENCY);
    window.setTimeout(Util.showSpinner,UPDATE_FREQUENCY-1000);
  },
  update: function () {
    new Ajax.Request('xml-rpc?method=stats',{method: 'get',onComplete: Updater.rsStats});          
  },
  rsStats: function(request) {
    Util.hideSpinner();
    ['service','stat'].each(function (tag) {
      $A(request.responseXML.getElementsByTagName(tag)).each(function (element) {
        var node = $(Element.textContent(element.firstChild).toLowerCase().replace(/\ /,'_'));  
        node.replaceChild(document.createTextNode(Element.textContent(element.childNodes[1])),node.firstChild);
      });
    });  
    var thread = $A(request.responseXML.getElementsByTagName('thread'));
    var threadTable = new Table('thread');
    threadTable.removeTBodyRows();
    var users = 0;
    thread.each(function (element) {
      users++;
      row = [];
      row.push(Element.textContent(element.childNodes[1]));
      row.push(Element.textContent(element.childNodes[2]));
      threadTable.addTbodyRow(row);    
    });
    // $('session_count').replaceChild(document.createTextNode(users + ' Connected Users'),$('session_count').firstChild);
    Updater.start();
}
  
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