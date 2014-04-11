var MOUSE_DOWN = 1, MOUSE_UP = 2, MOUSE_MOVE = 3;
var sendBuffer = new Int32Array(3);   
var mouseDown = false;
var count = 4; //use this to send only 1/4 of the move events, makes it
               //more responsive
function sendMouseEvent(m, e) {
  if(m == MOUSE_MOVE) if(--count > 0) return;
  e = e || window.event;
  sendBuffer[0] = m;
  if(m == MOUSE_MOVE || m == MOUSE_UP) {
    sendBuffer[1] = 1.2 * e.clientX;
    sendBuffer[2] = 1.2 * e.clientY;
  } else {
    sendBuffer[1] = 1.2 * e.clientX;
    sendBuffer[2] = 1.2 * e.clientY;
  } 
  // if(!websocket || websocket.readyState != websocket.open) {
  //   console.log(sendBuffer[0] + " " +
  //               sendBuffer[1] + " " +
  //               sendBuffer[1]);
  //   //return;              
  // }  
  websocket.send(sendBuffer);
   if(m == MOUSE_MOVE) count = 4;
}
window.onmousedown = function(e) {
  mouseDown = true;
  sendMouseEvent(MOUSE_DOWN, e);                    
}
window.onmouseup = function(e) {
  mouseDown = false;
  sendMouseEvent(MOUSE_UP, e);                    
}
window.onmousemove = function(e) {
  if(!mouseDown) return;
  sendMouseEvent(MOUSE_MOVE, e);                    
}