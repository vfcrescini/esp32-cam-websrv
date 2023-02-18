// 2023-01-14 script.js
// Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from:
//   https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Camera/CameraWebServer/camera_index.h
//   Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//   Licensed under the Apache License, Version 2.0

function genId()
{
  return Date.now().toString(16).padStart(12, "0") + parseInt(Math.random() * 100000000, 10).toString(16).padStart(8, "0");
}

function onContentLoaded(event)
{
  var baseHost = document.location.origin;

  const hide = el =>
  {
    el.classList.add('hidden');
  };
  const show = el =>
  {
    el.classList.remove('hidden');
  };

  const disable = el =>
  {
    el.classList.add('disabled');
    el.disabled = true;
  };

  const enable = el =>
  {
    el.classList.remove('disabled');
    el.disabled = false;
  };

  const updateValue = (el, value, updateRemote) =>
  {
    updateRemote = updateRemote == null ? true : updateRemote;
    let initialValue;
    if (el.type === 'checkbox')
    {
      initialValue = el.checked;
      value = !!value;
      el.checked = value;
    } else
    {
      initialValue = el.value;
      el.value = value;
    }

    if (updateRemote && initialValue !== value)
    {
      updateConfig(el);
    } else if(!updateRemote)
    {
      if(el.id === "aec")
      {
        value ? hide(exposure) : show(exposure);
      }
      else if(el.id === "agc")
      {
        if (value)
	{
          if (cam_type == "ov2460")
	  {
            show(gainCeiling);
          }
          hide(agcGain);
        }
	else
	{
          if (cam_type == "ov2460")
	  {
            hide(gainCeiling);
          }
          show(agcGain);
        }
      }
      else if(el.id === "awb_gain")
      {
        value ? show(wb) : hide(wb);
      }
    }
  }

  function updateConfig (el)
  {
    let value;
    switch (el.type)
    {
      case 'checkbox':
        value = el.checked ? 1 : 0;
        break
      case 'range':
      case 'select-one':
        value = el.value;
        break;
      case 'button':
      case 'submit':
        value = '1';
        break;
      default:
        return;
    }

    const query = `${baseHost}/control?var=${el.id}&val=${value}`;

    fetch(query).then(
      response =>
      {
        console.log(`request to ${query} finished, status: ${response.status}`);
      }
    )
  }

  document.querySelectorAll('.close').forEach(
    el =>
    {
      el.onclick = () =>
      {
        hide(el.parentNode);
      }
    }
  )

  // read initial values
  fetch(`${baseHost}/status`).then(
    function(response)
    {
      return response.json();
    }
  ).then(
    function(state)
    {
      document.querySelectorAll('.default-action').forEach(
        el =>
	{
          updateValue(el, state[el.id], false);
        }
      )
    }
  )

  const view = document.getElementById('stream');
  const viewContainer = document.getElementById('stream-container');
  const stillButton = document.getElementById('get-still');
  const streamButton = document.getElementById('toggle-stream');
  const closeButton = document.getElementById('close-stream');
  var is_streaming = false;

  const stopStream = () =>
  {
    window.stop();
    streamButton.innerHTML = 'Start Stream';
    is_streaming = false;
  };

  const startStream = () =>
  {
    view.setAttribute("src", baseHost + "/stream?id=" + genId());
    show(viewContainer);
    streamButton.innerHTML = 'Stop Stream';
    is_streaming = true;
  };

  stillButton.onclick = () =>
  {
    if (is_streaming)
    {
      stopStream();
    }
    else
    {
      view.setAttribute("src", baseHost + "/capture?id=" + genId());
      show(viewContainer);
    }
  };

  closeButton.onclick = () =>
  {
    stopStream();
    hide(viewContainer);
  };

  streamButton.onclick = () =>
  {
    if (is_streaming)
    {
      stopStream()
    }
    else
    {
      startStream()
    }
  };

  // Attach default on change action
  document.querySelectorAll('.default-action').forEach(
    el =>
    {
      el.onchange = () => updateConfig(el)
    }
  );

  // Custom actions
  // Gain
  const agc = document.getElementById('agc');
  const agcGain = document.getElementById('agc_gain-group');
  const gainCeiling = document.getElementById('gainceiling-group');
  agc.onchange = () =>
  {
    updateConfig(agc);
    if (agc.checked)
    {
      if (cam_type == "ov2460")
      {
        show(gainCeiling);
      }
      hide(agcGain);
    }
    else
    {
      if (cam_type == "ov2460")
      {
        hide(gainCeiling);
      }
      show(agcGain);
    }
  }

  // Exposure
  const aec = document.getElementById('aec');
  const exposure = document.getElementById('aec_value-group');
  aec.onchange = () =>
  {
    updateConfig(aec);
    aec.checked ? hide(exposure) : show(exposure);
  }

  // AWB
  const awb = document.getElementById('awb_gain');
  const wb = document.getElementById('wb_mode-group');
  awb.onchange = () =>
  {
    updateConfig(awb);
    awb.checked ? show(wb) : hide(wb);
  }

  // framesize
  const framesize = document.getElementById('framesize')

  framesize.onchange = () =>
  {
    updateConfig(framesize);
  };
}

document.addEventListener('DOMContentLoaded', onContentLoaded);
