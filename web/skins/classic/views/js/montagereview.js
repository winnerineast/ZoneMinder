function evaluateLoadTimes() {
  // Only consider it a completed event if we load ALL monitors, then zero all and start again
  var start=0;
  var end=0;
  if ( liveMode != 1 && currentSpeed == 0 ) return;  // don't evaluate when we are not moving as we can do nothing really fast.
  for ( var i = 0; i < monitorIndex.length; i++ ) {
    if ( monitorName[i] > "" ) {
      if ( monitorLoadEndTimems[i] ==0 ) return;   // if we have a monitor with no time yet just wait
      if ( start == 0 || start > monitorLoadStartTimems[i] ) start = monitorLoadStartTimems[i];
      if ( end   == 0 || end   < monitorLoadEndTimems[i]   ) end   = monitorLoadEndTimems[i];
    }
  }
  if ( start == 0 || end == 0 ) return; // we really should not get here
  for ( var i=0; i < numMonitors; i++ ) {
    var monId = monitorPtr[i];
    monitorLoadStartTimems[monId] = 0;
    monitorLoadEndTimems[monId] = 0;
  }
  freeTimeLastIntervals[imageLoadTimesEvaluated++] = 1 - ((end - start)/currentDisplayInterval);
  if( imageLoadTimesEvaluated < imageLoadTimesNeeded ) return;
  var avgFrac=0;
  for ( var i=0; i < imageLoadTimesEvaluated; i++ )
    avgFrac += freeTimeLastIntervals[i];
  avgFrac = avgFrac / imageLoadTimesEvaluated;
  // The larger this is(positive) the faster we can go
  if      (avgFrac >= 0.9)  currentDisplayInterval = (currentDisplayInterval * 0.50).toFixed(1);  // we can go much faster
  else if (avgFrac >= 0.8)  currentDisplayInterval = (currentDisplayInterval * 0.55).toFixed(1);
  else if (avgFrac >= 0.7)  currentDisplayInterval = (currentDisplayInterval * 0.60).toFixed(1);
  else if (avgFrac >= 0.6)  currentDisplayInterval = (currentDisplayInterval * 0.65).toFixed(1);
  else if (avgFrac >= 0.5)  currentDisplayInterval = (currentDisplayInterval * 0.70).toFixed(1);
  else if (avgFrac >= 0.4)  currentDisplayInterval = (currentDisplayInterval * 0.80).toFixed(1);
  else if (avgFrac >= 0.35) currentDisplayInterval = (currentDisplayInterval * 0.90).toFixed(1);
  else if (avgFrac >= 0.3)  currentDisplayInterval = (currentDisplayInterval * 1.00).toFixed(1);
  else if (avgFrac >= 0.25) currentDisplayInterval = (currentDisplayInterval * 1.20).toFixed(1);
  else if (avgFrac >= 0.2)  currentDisplayInterval = (currentDisplayInterval * 1.50).toFixed(1);
  else if (avgFrac >= 0.1)  currentDisplayInterval = (currentDisplayInterval * 2.00).toFixed(1);
  else currentDisplayInterval                      = (currentDisplayInterval * 2.50).toFixed(1);
  currentDisplayInterval=Math.min(Math.max(currentDisplayInterval, 30),10000);   // limit this from about 30fps to .1 fps
  imageLoadTimesEvaluated=0;
  setSpeed(speedIndex);
  $('fps').innerHTML="Display refresh rate is " + (1000 / currentDisplayInterval).toFixed(1) + " per second, avgFrac=" + avgFrac.toFixed(3) + ".";
}

function SetImageSource( monId, val ) {
  if ( liveMode == 1 ) {
    return monitorImageObject[monId].src.replace(/rand=\d+/i, 'rand='+Math.floor((Math.random() * 1000000) ));

  } else {
    for ( var i=0, eIdlength = eId.length; i < eIdlength; i++ ) {
      // Search for a match 
      if ( eMonId[i] == monId && val >= eStartSecs[i] && val <= eEndSecs[i] ) {
        var frame = parseInt((val - eStartSecs[i])/(eEndSecs[i]-eStartSecs[i])*eventFrames[i])+1;
        return "index.php?view=image&eid=" + eId[i] + '&fid='+frame + "&width=" + monitorCanvasObj[monId].width + "&height=" + monitorCanvasObj[monId].height;
      }
    } // end for
    return "no data";
  }
}

// callback when loading an image. Will load itself to the canvas, or draw no data
function imagedone( obj, monId, success ) {
  if ( success ) {
    var canvasCtx = monitorCanvasCtx[monId];
    var canvasObj = monitorCanvasObj[monId];

    canvasCtx.drawImage( monitorImageObject[monId], 0, 0, canvasObj.width, canvasObj.height );
    var iconSize=(Math.max(canvasObj.width, canvasObj.height) * 0.10);
    canvasCtx.font = "600 " + iconSize.toString() + "px Arial";
    canvasCtx.fillStyle = "white";
    canvasCtx.globalCompositeOperation = "difference";
    canvasCtx.fillText( "+", iconSize*0.2, iconSize*1.2 );
    canvasCtx.fillText( "-", canvasObj.width - iconSize*1.2, iconSize*1.2 );
    canvasCtx.globalCompositeOperation = "source-over";
    monitorLoadEndTimems[monId] = new Date().getTime(); // elapsed time to load
    evaluateLoadTimes();
  }
  monitorLoading[monId] = false;
  if ( ! success ) {
    // if we had a failrue queue up the no-data image
    //loadImage2Monitor(monId,"no data");  // leave the staged URL if there is one, just ignore it here.
    loadNoData( monId );
  } else {
    if ( monitorLoadingStageURL[monId] == "" ) {
      console.log("Not showing image for " + monId );
      // This means that there wasn't a loading image placeholder.
      // So we weren't actually loading an image... which seems weird.
      return;
    }
    //loadImage2Monitor(monId,monitorLoadingStageURL[monId] );
    //monitorLoadingStageURL[monId]="";
  }
  return;
}

function loadNoData( monId ) {
  if ( monId ) {
    var canvasCtx = monitorCanvasCtx[monId];
    var canvasObj = monitorCanvasObj[monId];
    canvasCtx.fillStyle="white";
    canvasCtx.fillRect(0, 0, canvasObj.width, canvasObj.height);
    var textSize=canvasObj.width * 0.15;
    var text="No Data";
    canvasCtx.font = "600 " + textSize.toString() + "px Arial";
    canvasCtx.fillStyle="black";
    var textWidth = canvasCtx.measureText(text).width;
    canvasCtx.fillText(text,canvasObj.width/2 - textWidth/2,canvasObj.height/2);
  } else {
    console.log("No monId in loadNoData");
  }
}

// Either draws the 
function loadImage2Monitor( monId, url ) {
  if ( monitorLoading[monId] && monitorImageObject[monId].src != url ) {
    // never queue the same image twice (if it's loading it has to be defined, right?
    monitorLoadingStageURL[monId] = url;   // we don't care if we are overriting, it means it didn't change fast enough
  } else {
    if ( monitorImageObject[monId].src == url ) return;   // do nothing if it's the same
    if ( url == 'no data' ) {
      loadNoData( monId );
    } else {
      monitorLoading[monId] = true;
      monitorLoadStartTimems[monId] = new Date().getTime();
      monitorImageObject[monId].src = url;  // starts a load but doesn't refresh yet, wait until ready
    }
  }
}
function timerFire() {
  // See if we need to reschedule
  if(currentDisplayInterval != timerInterval || currentSpeed == 0) {
    // zero just turn off interrupts
    clearInterval(timerObj);
    timerInterval=currentDisplayInterval;
    if(currentSpeed>0 || liveMode!=0) timerObj=setInterval(timerFire,timerInterval);  // don't fire out of live mode if speed is zero
  }

  if (liveMode) outputUpdate(currentTimeSecs); // In live mode we basically do nothing but redisplay
  else if (currentTimeSecs + playSecsperInterval >= maxTimeSecs) // beyond the end just stop
  {
    setSpeed(0);
    outputUpdate(currentTimeSecs);
  }
  else outputUpdate(currentTimeSecs + playSecsperInterval);
  return;
}

function drawSliderOnGraph(val) {
    var sliderWidth=10;
    var sliderLineWidth=1;
    var sliderHeight=cHeight;

    if(liveMode==1) {
        val=Math.floor( Date.now() / 1000);
    }
    // Set some sizes

    var labelpx = Math.max( 6, Math.min( 20, parseInt(cHeight * timeLabelsFractOfRow / (numMonitors+1)) ) );
    var labbottom=parseInt(cHeight * 0.2 / (numMonitors+1)).toString() + "px";  // This is positioning same as row labels below, but from bottom so 1-position
    var labfont=labelpx + "px Georgia";  // set this like below row labels

    if(numMonitors>0) {
      // if we have no data to display don't do the slider itself 
        var sliderX=parseInt( (val - minTimeSecs) / rangeTimeSecs * cWidth - sliderWidth/2);  // position left side of slider
        if(sliderX < 0) sliderX=0;
        if(sliderX+sliderWidth > cWidth) sliderX=cWidth-sliderWidth-1;

        // If we have data already saved first restore it from LAST time

        if(typeof underSlider !== 'undefined')
        {
            ctx.putImageData(underSlider,underSliderX, 0, 0, 0, sliderWidth, sliderHeight);
            underSlider=undefined;
        }
        if(liveMode==0)  // we get rid of the slider if we switch to live (since it may not be in the "right" place)
        {
            // Now save where we are putting it THIS time
            underSlider=ctx.getImageData(sliderX, 0, sliderWidth, sliderHeight);
            // And add in the slider'
            ctx.lineWidth=sliderLineWidth;
            ctx.strokeStyle='black';
            // looks like strokes are on the outside (or could be) so shrink it by the line width so we replace all the pixels
            ctx.strokeRect(sliderX+sliderLineWidth,sliderLineWidth,sliderWidth - 2*sliderLineWidth, sliderHeight - 2*sliderLineWidth);
            underSliderX=sliderX;
        }
        var o = $('scruboutput');
        if(liveMode==1)
        {
            o.innerHTML="Live Feed @ " + (1000 / currentDisplayInterval).toFixed(1) + " fps";
            o.style.color="red";
        }
        else
        {
            o.innerHTML=secs2dbstr(val);
            o.style.color="blue";
        }
        o.style.position="absolute";
        o.style.bottom=labbottom;
        o.style.font=labfont;
        // try to get length and then when we get too close to the right switch to the left
        var len = o.offsetWidth;
        var x;
        if(sliderX > cWidth/2)
            x=sliderX - len - 10;
        else
            x=sliderX + 10;
        o.style.left=x.toString() + "px";
    }

    // This displays (or not) the left/right limits depending on how close the slider is.
    // Because these change widths if the slider is too close, use the slider width as an estimate for the left/right label length (i.e. don't recalculate len from above)
    // If this starts to collide increase some of the extra space

    var o = $('scrubleft');
    o.innerHTML=secs2dbstr(minTimeSecs);
    o.style.position="absolute";
    o.style.bottom=labbottom;
    o.style.font=labfont;
    o.style.left="5px";
    if(numMonitors==0)  // we need a len calculation if we skipped the slider
        len = o.offsetWidth;
    // If the slider will overlay part of this suppress (this is the left side)
    if(len + 10 > sliderX || cWidth < len * 4 )  // that last check is for very narrow browsers
        o.style.display="none";
    else
    {
        o.style.display="inline";
        o.style.display="inline-flex";  // safari won't take this but will just ignore
    }

    var o = $('scrubright');
    o.innerHTML=secs2dbstr(maxTimeSecs);
    o.style.position="absolute";
    o.style.bottom=labbottom;
    o.style.font=labfont;
    // If the slider will overlay part of this suppress (this is the right side)
    o.style.left=(cWidth - len - 15).toString() + "px";
    if(sliderX > cWidth - len - 20 || cWidth < len * 4 )
        o.style.display="none";
    else
    {
        o.style.display="inline";
        o.style.display="inline-flex";
    }
}

function drawGraph()
{
    var divWidth=$('timelinediv').clientWidth
    canvas.width = cWidth = divWidth;   // Let it float and determine width (it should be sized a bit smaller percentage of window)
    canvas.height=cHeight = parseInt(window.innerHeight * 0.10);
    if(eId.length==0)
    {
        ctx.font="40px Georgia";
        ctx.fillStyle="Black";
        ctx.globalAlpha=1;
        var t="No data found in range - choose differently";
        var l=ctx.measureText(t).width;
        ctx.fillText(t,(cWidth - l)/2, cHeight-10);
        underSlider=undefined;
        return;
    }
    var rowHeight=parseInt(cHeight / (numMonitors + 1) );  // Leave room for a scale of some sort

    // first fill in the bars for the events (not alarms)

    for(var i=0; i<eId.length; i++)  // Display all we loaded
    {
        var x1=parseInt( (eStartSecs[i] - minTimeSecs) / rangeTimeSecs * cWidth) ;        // round low end down
        var x2=parseInt( (eEndSecs[i]   - minTimeSecs) / rangeTimeSecs * cWidth + 0.5 ) ; // round high end up to be sure consecutive ones connect
        ctx.fillStyle=monitorColour[eMonId[i]];
        ctx.globalAlpha = 0.2;    // light color for background
        ctx.clearRect(x1,monitorIndex[eMonId[i]]*rowHeight,x2-x1,rowHeight);  // Erase any overlap so it doesn't look artificially darker
        ctx.fillRect (x1,monitorIndex[eMonId[i]]*rowHeight,x2-x1,rowHeight);
    }
    for(var i=0; (i<fScore.length) && (maxScore>0); i++)  // Now put in scored frames (if any)
    {
        var x1=parseInt( (fTimeFromSecs[i] - minTimeSecs) / rangeTimeSecs * cWidth) ;        // round low end down
        var x2=parseInt( (fTimeToSecs[i]   - minTimeSecs) / rangeTimeSecs * cWidth + 0.5 ) ; // round up
        if(x2-x1 < 2) x2=x1+2;    // So it is visible make them all at least this number of seconds wide
        ctx.fillStyle=monitorColour[fMonId[i]];
        ctx.globalAlpha = 0.4 + 0.6 * (1 - fScore[i]/maxScore);    // Background is scaled but even lowest is twice as dark as the background
        ctx.fillRect(x1,monitorIndex[fMonId[i]]*rowHeight,x2-x1,rowHeight);
    }
    for(var i=0; i<numMonitors; i++)  // Note that this may be a sparse array
    {
        ctx.font= parseInt(rowHeight * timeLabelsFractOfRow).toString() + "px Georgia";
        ctx.fillStyle="Black";
        ctx.globalAlpha=1;
        ctx.fillText(monitorName[monitorPtr[i]], 0, (i + 1 - (1 - timeLabelsFractOfRow)/2 ) * rowHeight );  // This should roughly center font in row
    }
    underSlider=undefined;   // flag we don't have a slider cached
    drawSliderOnGraph(currentTimeSecs);
    return;
}

function redrawScreen()
{
    if(fitMode==0) // if we fit, then monitors were absolutely positioned already (or will be) otherwise release them to float
    {
        for(var i=0; i<numMonitors; i++)
            monitorCanvasObj[monitorPtr[i]].style.position="";
        $('monitors').setStyle('height',"auto");
    }
    if(liveMode==1) // if we are not in live view switch to history -- this has to come before fit in case we re-establish the timeline
    {
        $('SpeedDiv').style.display="none";
        $('timelinediv').style.display="none";
        $('live').innerHTML="History";
        $('zoomin').style.display="none";
        $('zoomout').style.display="none";
        $('panleft').style.display="none";
        $('panright').style.display="none";

    }
    else  // switch out of liveview mode
    {
        $('SpeedDiv').style.display="inline";
        $('SpeedDiv').style.display="inline-flex";
        $('timelinediv').style.display=null;
        $('live').innerHTML="Live";
        $('zoomin').style.display="inline";
        $('zoomin').style.display="inline-flex";
        $('zoomout').style.display="inline";
        $('zoomout').style.display="inline-flex";
        $('panleft').style.display="inline";
        $('panleft').style.display="inline-flex";
        $('panright').style.display="inline";
        $('panright').style.display="inline-flex";
    }

    if(fitMode==1)
    {
        $('ScaleDiv').style.display="none";
        $('fit').innerHTML="Scale";
        var vh=window.innerHeight;
        var vw=window.innerWidth;
        var pos=$('monitors').getPosition();
        var mh=(vh - pos.y - $('fps').getSize().y);
        $('monitors').setStyle('height',mh.toString() + "px");  // leave a small gap at bottom
        if(maxfit2($('monitors').getSize().x,$('monitors').getSize().y) == 0)   /// if we fail to fix we back out of fit mode -- ??? This may need some better handling
            fitMode=1-fitMode;
    }
    else  // switch out of fit mode
    {
        $('ScaleDiv').style.display="inline";
        $('ScaleDiv').style.display="inline-flex";
        $('fit').innerHTML="Fit";
        setScale(currentScale);
    }
    drawGraph();
    outputUpdate(currentTimeSecs);
    timerFire();  // force a fire in case it's not timing
}


function outputUpdate(val)
{
    drawSliderOnGraph(val);
    for(var i=0; i<numMonitors; i++)
    {
            loadImage2Monitor(monitorPtr[i],SetImageSource(monitorPtr[i],val));
    }
    var currentTimeMS = new Date(val*1000);
    currentTimeSecs=val;
}


/// Found this here: http://stackoverflow.com/questions/55677/how-do-i-get-the-coordinates-of-a-mouse-click-on-a-canvas-element
function relMouseCoords(event){
    var totalOffsetX = 0;
    var totalOffsetY = 0;
    var canvasX = 0;
    var canvasY = 0;
    var currentElement = this;

    do{
        totalOffsetX += currentElement.offsetLeft - currentElement.scrollLeft;
        totalOffsetY += currentElement.offsetTop - currentElement.scrollTop;
    }
    while(currentElement = currentElement.offsetParent)

    canvasX = event.pageX - totalOffsetX;
    canvasY = event.pageY - totalOffsetY;

    return {x:canvasX, y:canvasY}
}
HTMLCanvasElement.prototype.relMouseCoords = relMouseCoords;

// These are the functions for mouse movement in the timeline.  Note that touch is treated as a mouse move with mouse down

var mouseisdown=false;
function mdown(event) {mouseisdown=true; mmove(event);}
function mup(event)   {mouseisdown=false;}
function mout(event)  {mouseisdown=false;} // if we go outside treat it as release
function tmove(event) {mouseisdown=true; mmove(event);}

function mmove(event) {
  if(mouseisdown) {
    // only do anything if the mouse is depressed while on the sheet
    var sec = minTimeSecs + rangeTimeSecs / event.target.width * event.target.relMouseCoords(event).x;
    outputUpdate(sec);
  }
}

function secs2dbstr (s)
{
    var st = (new Date(s * 1000)).format("%Y-%m-%d %H:%M:%S");
    return st;
}

function setFit(value)
{
    fitMode=value;
    redrawScreen();
}

function showScale(newscale) // updates slider only
{
    $('scaleslideroutput').innerHTML = parseFloat(newscale).toFixed(2).toString() + " x";
    return;
}

function setScale(newscale) // makes actual change
{
    showScale(newscale);
    for(var i=0; i<numMonitors; i++)
    {
        monitorCanvasObj[monitorPtr[i]].width=monitorWidth[monitorPtr[i]]*monitorNormalizeScale[monitorPtr[i]]*monitorZoomScale[monitorPtr[i]]*newscale;
        monitorCanvasObj[monitorPtr[i]].height=monitorHeight[monitorPtr[i]]*monitorNormalizeScale[monitorPtr[i]]*monitorZoomScale[monitorPtr[i]]*newscale;
    }
    currentScale=newscale;
}

function showSpeed(val) // updates slider only
{
    $('speedslideroutput').innerHTML = parseFloat(speeds[val]).toFixed(2).toString() + " x";
}

function setSpeed(val)   // Note parameter is the index not the speed
{
    var t;
    if(liveMode==1) return;  // we shouldn't actually get here but just in case
    currentSpeed=parseFloat(speeds[val]);
    speedIndex=val;
    playSecsperInterval = currentSpeed * currentDisplayInterval / 1000;
    showSpeed(val);
    if( timerInterval != currentDisplayInterval || currentSpeed == 0 )  timerFire(); // if the timer isn't firing we need to trigger it to update
}

function setLive(value)
{
    liveMode=value;
    redrawScreen();
}


//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
// The section below are to reload this program with new parameters

function clicknav(minSecs,maxSecs,arch,live) {// we use the current time if we can
  var now = new Date() / 1000;
  var minStr="";
  var maxStr="";
  var currentStr="";
  if ( minSecs > 0 ) {
    if(maxSecs > now)
      maxSecs = parseInt(now);
    maxStr="&maxTime=" + secs2dbstr(maxSecs);
  }
  if ( maxSecs > 0 )
    minStr="&minTime=" + secs2dbstr(minSecs);
  if ( maxSecs == 0 && minSecs == 0 ) {
    minStr="&minTime=01/01/1950 12:00:00";
    maxStr="&maxTime=12/31/2035 12:00:00";
  }
  var intervalStr="&displayinterval=" + currentDisplayInterval.toString();
  if ( minSecs && maxSecs ) {
    if ( currentTimeSecs > minSecs && currentTimeSecs < maxSecs )  // make sure time is in the new range
      currentStr="&current=" + secs2dbstr(currentTimeSecs);
  }

  var liveStr="&live=0";
  if ( live == 1 )
    liveStr="&live=1";

  var fitStr="&fit=0";
  if ( fitMode == 1 )
    fitStr="&fit=1";

  var zoomStr="";
  for ( var i=0; i < numMonitors; i++ )
    if ( monitorZoomScale[monitorPtr[i]] < 0.99 || monitorZoomScale[monitorPtr[i]] > 1.01 )  // allow for some up/down changes and just treat as 1 of almost 1
      zoomStr += "&z" + monitorPtr[i].toString() + "=" + monitorZoomScale[monitorPtr[i]].toFixed(2);

  var uri = "?view=" + currentView + fitStr + groupStr + minStr + maxStr + currentStr + intervalStr + liveStr + zoomStr + "&scale=" + document.getElementById("scaleslider").value + "&speed=" + speeds[$j("#speedslider").value];
  window.location = uri;
}

function lastHour() {
  var now = new Date() / 1000;
  clicknav(now - 3600 + 1, now,1,0);
}
function lastEight() {
  var now = new Date() / 1000;
  clicknav(now - 3600*8 + 1, now,1,0);
}
function zoomin() {
  rangeTimeSecs = parseInt(rangeTimeSecs / 2);
  minTimeSecs = parseInt(currentTimeSecs - rangeTimeSecs/2);  // this is the slider current time, we center on that
  maxTimeSecs = parseInt(currentTimeSecs + rangeTimeSecs/2);
  clicknav(minTimeSecs,maxTimeSecs,1,0);
}

function zoomout() {
  rangeTimeSecs = parseInt(rangeTimeSecs * 2);
  minTimeSecs = parseInt(currentTimeSecs - rangeTimeSecs/2);  // this is the slider current time, we center on that
  maxTimeSecs = parseInt(currentTimeSecs + rangeTimeSecs/2);
  clicknav(minTimeSecs,maxTimeSecs,1,0);
}
function panleft() {
  minTimeSecs = parseInt(minTimeSecs - rangeTimeSecs/2);
  maxTimeSecs = minTimeSecs + rangeTimeSecs - 1;
  clicknav(minTimeSecs,maxTimeSecs,1,0);
}
function panright() {
  minTimeSecs = parseInt(minTimeSecs + rangeTimeSecs/2);
  maxTimeSecs = minTimeSecs + rangeTimeSecs - 1;
  clicknav(minTimeSecs,maxTimeSecs,1,0);
}
function allof() {
  clicknav(0,0,1,0);
}
function allnon() {
  clicknav(0,0,0,0);
}
/// >>>>>>>>>>>>>>>>> handles packing different size/aspect monitors on screen    <<<<<<<<<<<<<<<<<<<<<<<<

function compSize(a, b) { // sort array by some size parameter  - height seems to work best.  A semi-greedy algorithm
  var a_value = monitorHeight[a] * monitorWidth[a] * monitorNormalizeScale[a] * monitorZoomScale[a] * monitorNormalizeScale[a] * monitorZoomScale[a];
  var b_value = monitorHeight[b] * monitorWidth[b] * monitorNormalizeScale[b] * monitorZoomScale[b] * monitorNormalizeScale[b] * monitorZoomScale[b];

  if ( a_value > b_value ) return -1;
  else if ( a_value == b_value )  return 0;
  else return 1;
}


function maxfit2(divW, divH) {
  var bestFitX=[];   // how we arranged the so-far best match
  var bestFitX2=[];
  var bestFitY=[];
  var bestFitY2=[];
  var bestFitScale;

  var minScale=0.05;
  var maxScale=5.00;
  var bestFitArea=0;

  var borders=-1;

  monitorPtr.sort(compSize);

  while(1) {
    if( maxScale - minScale < 0.01 ) break;
    var thisScale = (maxScale + minScale) / 2;
    var allFit=1;
    var thisArea=0;
    var thisX=[];  // top left
    var thisY=[];
    var thisX2=[];  // bottom right
    var thisY2=[];

    for ( var m = 0; m < numMonitors; m++ ) {
      // this loop places each monitor (if it can)
      var monId = monitorPtr[m];

      function doesItFit(x,y,w,h,d) {  // does block (w,h) fit at position (x,y) relative to edge and other nodes already done (0..d)
        if(x+w>=divW) return 0;
        if(y+h>=divH) return 0;
        for(var i=0; i<=d; i++)
          if( !( thisX[i]>x+w-1 || thisX2[i] < x || thisY[i] > y+h-1 || thisY2[i] < y ) ) return 0;
        return 1; // it's OK
      }

      if ( borders <= 0 )
        borders=$("Monitor"+monId).getStyle("border").toInt() * 2;   // assume fixed size border, and added to both sides and top/bottom
      // try fitting over first, then down.  Each new one must land at either upper right or lower left corner of last (try in that order)
      // Pick the one with the smallest Y, then smallest X if Y equal
      var fitX = 999999999;
      var fitY = 999999999;
      for ( adjacent = 0; adjacent < m; adjacent ++ ) {
        // try top right of adjacent
        if ( doesItFit(thisX2[adjacent]+1, thisY[adjacent], monitorWidth[monId] * thisScale * monitorNormalizeScale[monId] * monitorZoomScale[monId] + borders, monitorHeight[monId] * thisScale * monitorNormalizeScale[monId] * monitorZoomScale[monId] + borders, m-1) == 1 ) {
          if ( thisY[adjacent]<fitY || ( thisY[adjacent] == fitY && thisX2[adjacent]+1 < fitX ) ) {
            fitX = thisX2[adjacent] + 1;
            fitY = thisY[adjacent];
          }
        }
        // try bottom left
        if ( doesItFit(thisX[adjacent], thisY2[adjacent]+1, monitorWidth[monId] * thisScale * monitorNormalizeScale[monId] * monitorZoomScale[monId] + borders, monitorHeight[monId] * thisScale * monitorNormalizeScale[monId] * monitorZoomScale[monId] + borders, m-1) == 1 ) {
          if ( thisY2[adjacent]+1 < fitY || ( thisY2[adjacent]+1 == fitY && thisX[adjacent] < fitX ) ) {
            fitX = thisX[adjacent];
            fitY = thisY2[adjacent] + 1;
          }
        }
      }
      if ( m == 0 ) { // note for the very first one there were no adjacents so the above loop didn't run
        if ( doesItFit(0,0,monitorWidth[monId] * thisScale * monitorNormalizeScale[monId] * monitorZoomScale[monId] + borders, monitorHeight[monId] * thisScale * monitorNormalizeScale[monId] * monitorZoomScale[monId] + borders, -1) == 1 ) {
          fitX = 0;
          fitY = 0;
        }
      }
      if ( fitX == 999999999 ) {
        allFit = 0;
        break; // break out of monitor loop flagging we didn't fit
      }
      thisX[m] =fitX;
      thisX2[m]=fitX + monitorWidth[monitorPtr[m]]  * thisScale * monitorNormalizeScale[monitorPtr[m]] * monitorZoomScale[monitorPtr[m]] + borders;
      thisY[m] =fitY;
      thisY2[m]=fitY + monitorHeight[monitorPtr[m]] * thisScale * monitorNormalizeScale[monitorPtr[m]] * monitorZoomScale[monitorPtr[m]] + borders;
      thisArea += (thisX2[m] - thisX[m])*(thisY2[m] - thisY[m]);
    }
    if ( allFit == 1 ) {
      minScale=thisScale;
      if(bestFitArea<thisArea) {
        bestFitArea=thisArea;
        bestFitX=thisX;
        bestFitY=thisY;
        bestFitX2=thisX2;
        bestFitY2=thisY2;
        bestFitScale=thisScale;
      }
    } else {
      // didn't fit
      maxScale=thisScale;
    }
  }
  if ( bestFitArea > 0 ) { // only rearrange if we could fit -- otherwise just do nothing, let them start coming out, whatever
    for ( m = 0; m < numMonitors; m++ ) {
      c = $("Monitor" + monitorPtr[m]);
      c.style.position="absolute";
      c.style.left=bestFitX[m].toString() + "px";
      c.style.top=bestFitY[m].toString() + "px";
      c.width = bestFitX2[m] - bestFitX[m] + 1 - borders;
      c.height= bestFitY2[m] - bestFitY[m] + 1 - borders;
    }
    return 1;
  } else {
    return 0;
  }
}

// >>>>>>>>>>>>>>>> Handles individual monitor clicks and navigation to the standard event/watch display

function showOneMonitor(monId) {
  // link out to the normal view of one event's data
  // We know the monitor, need to determine the event based on current time
  var url;
  if ( liveMode != 0 )
    url="?view=watch&mid=" + monId.toString();
  else
    for ( var i=0, len=eId.length; i<len; i++ ) {
      if ( eMonId[i] == monId && currentTimeSecs >= eStartSecs[i] && currentTimeSecs <= eEndSecs[i] )
        url="?view=event&eid=" + eId[i] + '&fid=' + parseInt(Math.max(1, Math.min(eventFrames[i], eventFrames[i] * (currentTimeSecs - eStartSecs[i]) / (eEndSecs[i] - eStartSecs[i] + 1) ) ));
        break;
    }
    createPopup(url, 'zmEvent', 'event', monitorWidth[eMonId[i]], monitorHeight[eMonId[i]]);
}

function zoom(monId,scale) {
  var lastZoomMonPriorScale = monitorZoomScale[monId];
  monitorZoomScale[monId] *= scale;
  if ( redrawScreen() == 0 ) {// failure here is probably because we zoomed too far
    monitorZoomScale[monId] = lastZoomMonPriorScale;
    alert("You can't zoom that far -- rolling back");
    redrawScreen();  // put things back and hope it works
  }
}

function clickMonitor(event,monId) {
  var monitor_element = $("Monitor"+monId.toString());
  var pos_x = event.offsetX ? (event.offsetX) : event.pageX - monitor_element.offsetLeft;
  var pos_y = event.offsetY ? (event.offsetY) : event.pageY - monitor_element.offsetTop;
  if ( pos_x < monitor_element.width/4 && pos_y < monitor_element.height/4 )
    zoom(monId,1.15);
  else if ( pos_x > monitor_element.width * 3/4 && pos_y < monitor_element.height/4 )
    zoom(monId,1/1.15);
  else
    showOneMonitor(monId);
  return;
}

// >>>>>>>>> Initialization that runs on window load by being at the bottom 

function initPage() {
  canvas = $("timeline");
  ctx = canvas.getContext('2d');
  for ( var i = 0, len = monitorPtr.length; i < len; i += 1 ) {
    var monId = monitorPtr[i];
    if ( ! monId ) continue;
    monitorCanvasObj[monId] = $('Monitor'+monId );
    if ( ! monitorCanvasObj[monId] ) {
      alert("Couldn't find DOM element for Monitor"+monId + "monitorPtr.length="+len);
    } else {
      monitorCanvasCtx[monId] = monitorCanvasObj[monId].getContext('2d');
      var imageObject = monitorImageObject[monId] = new Image();
      imageObject.monId = monId;
      imageObject.onload = function() {imagedone(this, this.monId, true )};
      imageObject.onerror = function() {imagedone(this, this.monId, false )};
      loadImage2Monitor( monId, monitorImageURL[monId] );
    }
  }
  drawGraph();
  setSpeed(speedIndex);
  setFit(fitMode);  // will redraw 
  setLive(liveMode);  // will redraw
}
window.addEventListener("resize",redrawScreen);
// Kick everything off
window.addEvent( 'domready', initPage );
