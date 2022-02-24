"use strict";

/*
 * mjpg_view.js
 * ============
 * 
 * Main program module for the MJPEG-Viewer webapp.
 */

// Wrap everything in an anonymous function that we immediately invoke
// after it is declared -- this prevents anything from being implicitly
// added to global scope
(function() {

  /*
   * Constants
   * =========
   */

  /*
   * The maximum safe integer value that can be stored in JavaScript.
   * 
   * This is pow(2, 53) - 1.  In recent browsers, this is the same as
   * the constant Number.MAX_SAFE_INTEGER.
   */
  var MAX_IVAL = 9007199254740991;

  /*
   * Local data
   * ==========
   */

  /*
   * Boolean flag indicating whether a video is currently loaded.
   */
  var m_loaded = false;
  
  /*
   * The actual M-JPEG video file object, only if m_loaded.
   */
  var m_mjpg;
  
  /*
   * The frame index, only if m_loaded.
   * 
   * This is a non-empty array, where each element is a byte offset
   * within the file stored at m_mjpg, indicating the start of a JPEG
   * frame within that stream.
   * 
   * Indices are in strictly ascending order.  Frame N starts at the
   * byte with offset [N] in the array, and ends one byte before the
   * start of the next frame, ([N + 1] - 1).  For the last frame, the
   * frame ends at the end of the M-JPEG stream.
   */
  var m_index;
  
  /*
   * The current, zero-based frame index, only if m_loaded.
   * 
   * Must be at least zero and less than m_index.length.
   * 
   * However, in the special case of when a new video has just been
   * loaded, this may have -1 to indicate no current frame loaded.
   */
  var m_pos;
  
  /*
   * The object URL to the current frame JPEG blob, only if m_loaded.
   * 
   * Don't forget to revoke this when appropriate.
   * 
   * In the special case that m_pos is -1, this is not defined yet.
   */
  var m_url;
  
  /*
   * Flag set to true when in the midst of processing a scrub event, to
   * prevent recursive invocations of the event handler.
   */
  var m_scrubbing = false;

  /*
   * Local functions
   * ===============
   */
  
  /*
   * Report an error to console and throw an exception for a fault
   * occurring within this module.
   *
   * Parameters:
   *
   *   func_name : string - the name of the function in this module
   *
   *   loc : number(int) - the location within the function
   */
  function fault(func_name, loc) {
    
    // If parameters not valid, set to unknown:0
    if ((typeof func_name !== "string") || (typeof loc !== "number")) {
      func_name = "unknown";
      loc = 0;
    }
    loc = Math.floor(loc);
    if (!isFinite(loc)) {
      loc = 0;
    }
    
    // Report error to console
    console.log("Fault at " + func_name + ":" + String(loc) +
                  " in mjv");
    
    // Throw exception
    throw ("mjv:" + func_name + ":" + String(loc));
  }
  
  /*
   * Escape a string if necessary so that it can be included within
   * HTML markup.
   *
   * This first escapes & as &amp;, and then escapes < as &lt; and > as
   * &gt;
   *
   * Parameters:
   *
   *   str : string - the string to escape
   *
   * Return:
   *
   *   the escaped string
   */
  function htmlEsc(str) {
    
    var func_name = "htmlEsc";
    
    // Check parameter
    if (typeof str !== "string") {
      fault(func_name, 100);
    }
    
    // Replace ampersand
    str = str.replace(/&/g, "&amp;");
    
    // Replace markup characters
    str = str.replace(/</g, "&lt;");
    str = str.replace(/>/g, "&gt;");
    
    // Return escaped string
    return str;
  }
  
  /*
   * Find the element with the given ID and set its display property to
   * "block" to show it.
   *
   * Assumes that the element is properly displayed with "block".
   *
   * Parameters:
   *
   *   elid : string - the ID of the element to show
   */
  function appear(elid) {
    
    var func_name = "appear";
    var e;
    
    // Check parameter
    if (typeof elid !== "string") {
      fault(func_name, 100);
    }
    
    // Get the element
    e = document.getElementById(elid);
    if (e == null) {
      fault(func_name, 200);
    }
    
    // Show the element
    e.style.display = "block";
  }

  /*
   * Find the element with the given ID and set its display property to
   * "none" to hide it.
   *
   * Parameters:
   *
   *   elid : string - the ID of the element to hide
   */
  function dismiss(elid) {
    
    var func_name = "dismiss";
    var e;
    
    // Check parameter
    if (typeof elid !== "string") {
      fault(func_name, 100);
    }
    
    // Get the element
    e = document.getElementById(elid);
    if (e == null) {
      fault(func_name, 200);
    }
    
    // Hide the element
    e.style.display = "none";
  }
  
  /*
   * Set the text displayed in the status box.
   * 
   * The string is trimmed of leading and trailing whitespace.  If
   * empty after trimming, &nbsp; is substituted.  Otherwise, the string
   * will be escaped if necessary by this function and then written to
   * the status box. 
   * 
   * Parameters:
   * 
   *   str - the new, (unescaped) text to set
   */
  function setStatus(str) {
    
    var func_name = "setStatus";
    var e;
    
    // Check parameter
    if (typeof str !== "string") {
      fault(func_name, 100);
    }
    
    // Trim the string
    str = str.trim();
    
    // Replace an empty string with &nbsp; else escape it
    if (str.length > 0) {
      str = htmlEsc(str);
    } else {
      str = "&nbsp;";
    }
    
    // Get the element
    e = document.getElementById("pStatus");
    if (e == null) {
      fault(func_name, 200);
    }
    
    // Set the internal text
    e.innerHTML = str;
  }

  /*
   * Given a DataView on top of the index file, read the 64-bit entry
   * number (i) from the view, where zero is the first 64-bit integer,
   * one is the second 64-bit integer, and so forth.
   * 
   * This function doesn't check its input parameters, so be careful.
   * 
   * Valid return values are in range [0, MAX_IVAL].  If the value in
   * the file is out of range, -1 is returned.
   * 
   * This function uses only 32-bit reads from the DataView, so it is
   * able to support the full MAX_IVAL range even if the underlying
   * browser does not support 64-bit integer operations on the DataView.
   * 
   * Parameters:
   * 
   *   dv - the DataView
   * 
   *   i - the integer index to read
   * 
   * Return:
   * 
   *   the integer value, or -1 if the value stored can't be represented
   *   as a JavaScript numeric value
   */
  function readIndexValue(dv, i) {
    
    var a, b;
    
    // Get the most-significant unsigned 32-bit value in "a" and the 
    // least-significant unsigned 32-bit value in "b"; use big endian
    // ordering for both
    a = dv.getUint32(i * 8, false);
    b = dv.getUint32((i * 8) + 4, false);

    // We only have room in the range for 21 bits in the most
    // significant dword; otherwise, return -1
    if (a > 2097151) {
      return -1;
    }
    
    // Range is fine, so combine into one value
    return ((a << 32) | b);
  }

  /*
   * Update the current frame position.
   * 
   * i is the new frame position index.  It must be a number, but may
   * have any numeric value, including non-finite values.
   * 
   * This call will be ignored if no video is currently loaded.  The
   * call will also be ignored if there is no actual change in position.
   * 
   * The given frame position is first floored to an integer.  Then, it
   * is changed to zero if it is not finite.  Finally, it is clamped so
   * that it is in valid frame index range.
   * 
   * Internal state will be updated to display the requested frame.  The
   * status box will be updated to show the current position, the frame
   * viewer will be displayed if currently hidden, and an <img> element
   * will be added to the viewer showing the current frame.  This also
   * handles showing and updating the navigation box.
   * 
   * Parameters:
   * 
   *   i : Number - the requested frame index
   */
  function updatePos(i) {
    
    var func_name = "updatePos";
    var eDIV, eIMG, e, f_begin, f_end;
    
    // Check parameter
    if (typeof(i) !== "number") {
      fault(func_name, 100);
    }
    
    // If nothing loaded, ignore this call
    if (!m_loaded) {
      return;
    }

    // Begin by correcting the value
    i = Math.floor(i);
    if (!isFinite(i)) {
      i = 0;
    }
    i = Math.min(Math.max(i, 0), m_index.length - 1);

    // Ignore the call if there has been no change in position
    if (m_pos === i) {
      return;
    }
    
    // Get the image viewer DIV
    eDIV = document.getElementById("divImage");
    if (eDIV == null) {
      fault(func_name, 200);
    }
    
    // Clear any elements that are currently in the viewer DIV
    while (eDIV.hasChildNodes()) {
      eDIV.removeChild(eDIV.firstChild);
    }
    
    // Unles m_pos is the special initial value of -1, begin by revoking
    // the current object URL
    if (m_pos >= 0) {
      URL.revokeObjectURL(m_url);
      m_url = false;
    }
    
    // Determine the beginning and end of the requested frame; the end
    // is the byte offset AFTER the last byte
    f_begin = m_index[i];
    if (i < m_index.length - 1) {
      f_end = m_index[i + 1];
    } else {
      f_end = m_mjpg.size;
    }

    // Update the internal state
    m_pos = i;
    m_url = URL.createObjectURL(
                  m_mjpg.slice(f_begin, f_end, "image/jpeg"));
    
    // Create an <img> for the current frame
    eIMG = document.createElement("img");
    eIMG.setAttribute("src", m_url);
    
    // Insert the <img> into the div
    eDIV.appendChild(eIMG);
    
    // Update the scrub slider
    e = document.getElementById("rngScrub");
    if (e == null) {
      fault(func_name, 300);
    }
    if (m_index.length > 1) {
      // More than one frame, so update slider appropriately
      e.value = 0;
      e.min = 0;
      e.max = m_index.length - 1;
      e.value = m_pos;
      
    } else {
      // Only a single frame, so we just define a dummy slider
      e.value = 0;
      e.min = 0;
      e.max = 1;
    }
    
    // Show image viewer and navigation boxes if not already displayed
    appear("divImage");
    appear("divNav");
    
    // Update status
    setStatus("Frame " + i + " / " + (m_index.length - 1));
  }

  /*
   * Public functions
   * ================
   */

  /*
   * Invoked when the user clicks the "Close" button, and also to close
   * any currently loaded video before a new video is loaded.
   * 
   * Also used when starting up to set initial state.
   */
  function close() {
    
    var func_name = "close";
    var eDIV;
    
    // Get the image viewer DIV
    eDIV = document.getElementById("divImage");
    if (eDIV == null) {
      fault(func_name, 100);
    }
    
    // Clear contents of image viewer
    while (eDIV.hasChildNodes()) {
      eDIV.removeChild(eDIV.firstChild);
    }
    
    // Hide image viewer and navigation
    dismiss("divImage");
    dismiss("divNav");
    
    // If we are currently loaded, revoke the URL of the current frame
    // blob
    if (m_loaded) {
      URL.revokeObjectURL(m_url);
    }
    
    // Clear everything to false, which also releases memory and file
    // objects
    m_loaded = false;
    m_mjpg = false;
    m_index = false;
    m_pos = false;
    m_url = false;
    
    // Update the status display
    setStatus("Nothing loaded");
  }

  /*
   * Invoked when the user scrubs the slider to a different position.
   */
  function handleScrub() {
    
    var func_name = "handleScrub";
    var e;
    
    // Ignore if nothing currently loaded
    if (!m_loaded) {
      return;
    }
    
    // Ignore if currently scrubbing
    if (m_scrubbing) {
      return;
    }
    
    // Get the slider control
    e = document.getElementById("rngScrub");
    if (e == null) {
      fault(func_name, 100);
    }
    
    // Update frame position with current value, using the scrubbing
    // guard to block recursive invocations of this event handler
    try {
      m_scrubbing = true;
      updatePos(parseInt(e.value, 10));
    } catch (ex) {
      m_scrubbing = false;
      throw(ex);
    }
    m_scrubbing = false;
  }

  /*
   * Invoked when in the midst of dragging the scrub slider.
   */
  function handleScrubbing() {
    
    var func_name = "handleScrubbing";
    var eRng, eSpan;
    
    // Get the slider control and the output span
    eRng = document.getElementById("rngScrub");
    if (eRng == null) {
      fault(func_name, 100);
    }
    
    eSpan = document.getElementById("spnScrubVal");
    if (eSpan == null) {
      fault(func_name, 110);
    }
    
    // Update current scrubbing value
    eSpan.innerHTML = eRng.value;
  }

  /*
   * Event handler for when files are dropped into the loading box.
   * 
   * This gets the data files and routes the call to loadMJPG().
   */
  function handleDrop(ev) {
    
    var fMJPG, fIndex;
    var fr, f, dv, ar, arl, i;
    
    // Handle this event
    ev.preventDefault();
    
    // Make sure we got two files
    if (ev.dataTransfer.files.length !== 2) {
      setStatus("ERROR: Expecting two files!");
      return;
    }
    
    // Get the two files, not caring about which is which for the moment
    fMJPG  = ev.dataTransfer.files.item(0);
    fIndex = ev.dataTransfer.files.item(1);
    
    // In every practical case, the index file is the smaller one, so
    // swap if necessary by looking at file sizes
    if (fIndex.size > fMJPG.size) {
      f = fMJPG;
      fMJPG = fIndex;
      fIndex = f;
    }
    
    // Index file size should be at least 16 and a multiple of eight
    if ((fIndex.size < 16) || ((fIndex.size % 8) !== 0)) {
      setStatus("ERROR: Invalid index file!");
      return;
    }
    
    // We are starting the asynchronous loading procedure, so update
    // the status and hide the load box
    setStatus("Loading...");
    dismiss("divLoad");
    
    // We want to read the whole index file
    fr = new FileReader();
    
    // If the read operation fails, update status and show the load
    // controls again
    f = function(evb) {
      setStatus("ERROR: Failed to read index file!");
      appear("divLoad");
    };
    fr.onabort = f;
    fr.onerror = f;
    
    // Function continues when the reading operation is complete
    fr.onload = function(evb) {
      
      // Asynchronous portion has completed, so re-show the loading box
      appear("divLoad");
      
      // Byte length must be at least 16 and a multiple of eight
      if ((fr.result.byteLength < 16) ||
            ((fr.result.byteLength % 8) !== 0)) {
        setStatus("ERROR: Invalid index file (Code 1)!");
        return;
      }
      
      // Create a DataView for reading integers
      dv = new DataView(fr.result);
      
      // Get the total number of elements from the first integer
      arl = readIndexValue(dv, 0);
      if (arl < 0) {
        setStatus("ERROR: Invalid index file (Code 2)!");
        return;
      }
      
      // Make sure that the length of the arraybuffer matches what is
      // suggested by the element count
      if ((fr.result.byteLength / 8) - 1 !== arl) {
        setStatus("ERROR: Invalid index file (Code 3)!");
        return;
      }
      
      // Allocate an array to store the index values
      ar = new Array(arl);
      
      // Copy everything into the index array, checking that everything
      // within JavaScript numeric range, the sequence is strictly
      // ascending, and everything within range of the M-JPEG file blob
      for(i = 0; i < arl; i++) {
        ar[i] = readIndexValue(dv, i + 1);
        if (ar[i] < 0) {
          setStatus("ERROR: Invalid index file (Code 4)!");
          return;
        }
        if (i > 0) {
          if (!(ar[i - 1] < ar[i])) {
            setStatus("ERROR: Invalid index file (Code 5)!");
            return;
          }
        }
        if (ar[i] >= fMJPG.size) {
          setStatus("ERROR: Invalid index file (Code 6)!");
          return;
        }
      }
      
      // We are ready to change the state, so begin by closing anything
      // that is currently loaded
      close();
      
      // Load initial state, using the special -1 code for m_pos to
      // indicate no frame loaded yet
      m_loaded = true;
      m_mjpg = fMJPG;
      m_index = ar;
      m_pos = -1;
      
      // Show the first frame
      updatePos(0);
    };
    
    // Begin asynchronously reading the index blob
    fr.readAsArrayBuffer(fIndex);
  }

  /*
   * Event handler for when data is being dragged over the file loading
   * box.
   * 
   * This activates the area to allow for drag-and-drop.
   */
  function handleDrag(ev) {
    ev.preventDefault();
    ev.dataTransfer.dropEffect = "copy";
  }

  /*
   * Invoked when the page has finished loading.
   */
  function handleLoad() {
    var e;
    var func_name = "handleLoad";
    
    // Set the initial, closed state
    close();
    
    // Finally, hide the splash screen and show the main DIV
    dismiss("divSplash");
    appear("divMain");
  }

  /*
   * Export declarations
   * ===================
   * 
   * All exports are declared within a global "mjv" object.
   */
  window.mjv = {
    "close": close,
    "handleScrub": handleScrub,
    "handleScrubbing": handleScrubbing,
    "handleDrop": handleDrop,
    "handleDrag": handleDrag,
    "handleLoad": handleLoad
  };

}());

// Since we loaded this script module with defer, this doesn't run until
// after the page has loaded the DOM, so we can start directly here by
// calling the loading procedure
//
mjv.handleLoad();
