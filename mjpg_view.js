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
   */
  var m_pos;
  
  /*
   * The object URL to the current frame JPEG blob, only if m_loaded.
   * 
   * Don't forget to revoke this when appropriate.
   */
  var m_url;

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
   * Invoked when the user clicks the "Load video" button.
   */
  function loadMJPG() {
    
    var func_name = "loadMJPG";
    var eFMJPG, eFIndex;
    
    // First of all, invoke close to reset the state
    close();
    
    // Get the file upload elements
    eFMJPG = document.getElementById("filMJPG");
    if (eFMJPG == null) {
      fault(func_name, 100);
    }
    
    eFIndex = document.getElementById("filIndex");
    if (eFIndex == null) {
      fault(func_name, 110);
    }
    
    // Make sure we have exactly one file selected in both file upload
    // elements
    if (eFMJPG.files.length !== 1) {
      setStatus("ERROR: Select one raw MJPG stream!");
      return;
    }
    if (eFIndex.files.length !== 1) {
      setStatus("ERROR: Select one MJPG index file!");
      return;
    }
    
    // We are starting the asynchronous loading procedure, so update
    // the status and hide the load controls
    setStatus("Loading...");
    dismiss("divLoad");
    
    // @@TODO:
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
    "loadMJPG": loadMJPG,
    "handleLoad": handleLoad
  };

}());

// Since we loaded this script module with defer, this doesn't run until
// after the page has loaded the DOM, so we can start directly here by
// calling the loading procedure
//
mjv.handleLoad();
