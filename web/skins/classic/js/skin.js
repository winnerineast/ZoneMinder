//
// ZoneMinder base static javascript file, $Date$, $Revision$
// Copyright (C) 2001-2008 Philip Coombes
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

//
// This file should only contain static JavaScript and no php.
// Use skin.js.php for JavaScript that need pre-processing
//

var popupOptions = "resizable,scrollbars,status=no";

function checkSize() {
  if (window.outerHeight) {
    var w = window.outerWidth;
    var prevW = w;
    var h = window.outerHeight;
    var prevH = h;
    if (h > screen.availHeight)
      h = screen.availHeight;
    if (w > screen.availWidth)
      w = screen.availWidth;
    if (w != prevW || h != prevH)
      window.resizeTo(w, h);
  }
}

// Deprecated
function newWindow( url, name, width, height ) {
  var windowId = window.open( url, name, popupOptions+",width="+width+",height="+height );
}

function getPopupSize( tag, width, height ) {
  var popupSize = Object.clone( popupSizes[tag] );
  if ( !popupSize ) {
    Error( "Can't find window size for tag '"+tag+"'" );
    return( { 'width': 0, 'height': 0 } );
  }
  if ( popupSize.width && popupSize.height ) {
    if ( width || height )
      Warning( "Ignoring passed dimensions "+width+"x"+height+" when getting popup size for tag '"+tag+"'" );
    return( popupSize );
  }
  if ( popupSize.addWidth ) {
    popupSize.width = popupSize.addWidth;
    if ( !width )
      Error( "Got addWidth but no passed width when getting popup size for tag '"+tag+"'" );
    else
      popupSize.width += parseInt(width);
  } else if ( width ) {
    popupSize.width = width;
    Error( "Got passed width but no addWidth when getting popup size for tag '"+tag+"'" );
  }
  if ( popupSize.minWidth && popupSize.width < popupSize.minWidth ) {
    Warning( "Adjusting to minimum width when getting popup size for tag '"+tag+"'" );
    popupSize.width = popupSize.minWidth;
  }
  if ( popupSize.addHeight ) {
    popupSize.height = popupSize.addHeight;
    if ( !height )
      Error( "Got addHeight but no passed height when getting popup size for tag '"+tag+"'" );
    else
      popupSize.height += parseInt(height);
  } else if ( height ) {
    popupSize.height = height;
    Error( "Got passed height but no addHeight when getting popup size for tag '"+tag+"'" );
  }
  if ( popupSize.minHeight && ( popupSize.height < popupSize.minHeight ) ) {
    Warning( "Adjusting to minimum height ("+popupSize.minHeight+") when getting popup size for tag '"+tag+"' because calculated height is " + popupSize.height );
    popupSize.height = popupSize.minHeight;
  }
  Debug( popupSize );
  return( popupSize );
}

function zmWindow() {
  var zmWin = window.open( 'http://www.zoneminder.com', 'ZoneMinder' );
  if ( ! zmWin ) {
    // if popup blocking is enabled, the popup won't be defined.
    console.log("Please disable popup blocking.");
  } else {
    zmWin.focus();
  }
}

function createPopup( url, name, tag, width, height ) {
  var popupSize = getPopupSize( tag, width, height );
  var popupDimensions = "";
  if ( popupSize.width > 0 )
    popupDimensions += ",width="+popupSize.width;
  if ( popupSize.height > 0 )
    popupDimensions += ",height="+popupSize.height;
  var popup = window.open( url, name, popupOptions+popupDimensions );
  if ( ! popup ) {
    // if popup blocking is enabled, the popup won't be defined.
    console.log("Please disable popup blocking.");
  } else {
    popup.focus();
  }
}

function createEventPopup( eventId, eventFilter, width, height ) {
  var url = '?view=event&eid='+eventId;
  if ( eventFilter )
    url += eventFilter;
  var name = 'zmEvent';
  var popupSize = getPopupSize( 'event', width, height );
  var popup = window.open( url, name, popupOptions+",width="+popupSize.width+",height="+popupSize.height );
  if ( ! popup ) {
    // if popup blocking is enabled, the popup won't be defined.
    console.log("Please disable popup blocking.");
  } else {
    popup.focus();
  }
}

function createFramesPopup( eventId, width, height ) {
  var url = '?view=frames&eid='+eventId;
  var name = 'zmFrames';
  var popupSize = getPopupSize( 'frames', width, height );
  var popup = window.open( url, name, popupOptions+",width="+popupSize.width+",height="+popupSize.height );
  if ( ! popup ) {
    // if popup blocking is enabled, the popup won't be defined.
    console.log("Please disable popup blocking.");
  } else {
    popup.focus();
  }
}

function createFramePopup( eventId, frameId, width, height ) {
  var url = '?view=frame&eid='+eventId+'&fid='+frameId;
  var name = 'zmFrame';
  var popupSize = getPopupSize( 'frame', width, height );
  var popup = window.open( url, name, popupOptions+",width="+popupSize.width+",height="+popupSize.height );
  if ( ! popup ) {
    // if popup blocking is enabled, the popup won't be defined.
    console.log("Please disable popup blocking.");
  } else {
    popup.focus();
  }
}

function windowToFront() {
  top.window.focus();
}

function closeWindow() {
  top.window.close();
}

function refreshWindow() {
  window.location.reload( true );
}

function refreshParentWindow() {
  if ( window.opener )
    window.opener.location.reload( true );
}

//Shows a message if there is an error in the streamObj or the stream doesn't exist.  Returns true if error, false otherwise.
function checkStreamForErrors( funcName, streamObj ) {
  if ( !streamObj ) {
    Error( funcName+": stream object was null" );
    return true;
  }
  if ( streamObj.result == "Error" ) {
    Error( funcName+" stream error: "+streamObj.message );
    return true;
  }
  return false;
}

function secsToTime( seconds ) {
  var timeString = "--";
  if ( seconds < 60 ) {
    timeString = seconds.toString();
  } else if ( seconds < 60*60 ) {
    var timeMins = parseInt(seconds/60);
    var timeSecs = seconds%60;
    if ( timeSecs < 10 )
      timeSecs = '0'+timeSecs.toString().substr( 0, 4 );
    else
      timeSecs = timeSecs.toString().substr( 0, 5 );
    timeString = timeMins+":"+timeSecs;
  } else {
    var timeHours = parseInt(seconds/3600);
    var timeMins = (seconds%3600)/60;
    var timeSecs = seconds%60;
    if ( timeMins < 10 )
      timeMins = '0'+timeMins.toString().substr( 0, 4 );
    else
      timeMins = timeMins.toString().substr( 0, 5 );
    if ( timeSecs < 10 )
      timeSecs = '0'+timeSecs.toString().substr( 0, 4 );
    else
      timeSecs = timeSecs.toString().substr( 0, 5 );
    timeString = timeHours+":"+timeMins+":"+timeSecs;
  }
  return( timeString );
}

function submitTab( tab ) {
  var form = $('contentForm');
  form.action.value = "";
  form.tab.value = tab;
  form.submit();
}

function configureDeleteButton( element ) {
  var form = element.form;
  var checked = element.checked;
  if ( !checked ) {
    for ( var i = 0; i < form.elements.length; i++ ) {
      if ( form.elements[i].name == element.name ) {
        if ( form.elements[i].checked ) {
          checked = true;
          break;
        }
      }
    }
  }
  form.deleteBtn.disabled = !checked;
}

function confirmDelete( message ) {
  return( confirm( message?message:'Are you sure you wish to delete?' ) );
}

if ( refreshParent ) {
  refreshParentWindow();
}

if ( focusWindow ) {
  windowToFront();
}
window.addEvent( 'domready', checkSize);

function convertLabelFormat(LabelFormat, monitorName){
	//convert label format from strftime to moment's format (modified from
	//https://raw.githubusercontent.com/benjaminoakes/moment-strftime/master/lib/moment-strftime.js
	//added %f and %N below (TODO: add %Q)
	var replacements = { a: 'ddd', A: 'dddd', b: 'MMM', B: 'MMMM', d: 'DD', e: 'D', F: 'YYYY-MM-DD', H: 'HH', I: 'hh', j: 'DDDD', k: 'H', l: 'h', m: 'MM', M: 'mm', p: 'A', S: 'ss', u: 'E', w: 'd', W: 'WW', y: 'YY', Y: 'YYYY', z: 'ZZ', Z: 'z', 'f': 'SS', 'N': "["+monitorName+"]", '%': '%' };
	var momentLabelFormat = Object.keys(replacements).reduce(function (momentFormat, key) {
	      var value = replacements[key];
	      return momentFormat.replace("%" + key, value);
	}, LabelFormat);
	return momentLabelFormat;
}

function addVideoTimingTrack(video, LabelFormat, monitorName, duration, startTime){
	var labelFormat = convertLabelFormat(LabelFormat, monitorName);
	var webvttformat = 'HH:mm:ss.SSS', webvttdata="WEBVTT\n\n";

	startTime = moment(startTime);

	var seconds = moment({s:0}), endduration = moment({s:duration});
	while(seconds.isBefore(endduration)){
		webvttdata += seconds.format(webvttformat) + " --> ";
		seconds.add(1,'s');
		webvttdata += seconds.format(webvttformat) + "\n";
		webvttdata += startTime.format(labelFormat) + "\n\n";
		startTime.add(1, 's');
	}
	var track = document.createElement('track');
	track.kind = "captions";
	track.srclang = "en";
	track.label = "English";
	track['default'] = true;
	track.src = 'data:plain/text;charset=utf-8,'+encodeURIComponent(webvttdata);
	video.appendChild(track);
}
