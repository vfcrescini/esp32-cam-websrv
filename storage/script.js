// 2023-01-14 script.js
// Vino Fernando Crescini  <vfcrescini@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from:
//   https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Camera/CameraWebServer/camera_index.h
//   Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//   Licensed under the Apache License, Version 2.0

function onContentLoaded(event)
{
  const view = document.getElementById('stream');
  const viewContainer = document.getElementById('stream-container');
  const resetButton = document.getElementById('reset');
  const stillButton = document.getElementById('get-still');
  const streamButton = document.getElementById('toggle-stream');
  const closeButton = document.getElementById('close-stream');
  const agc = document.getElementById('agc');
  const agcGain = document.getElementById('agc_gain-group');
  const gainCeiling = document.getElementById('gainceiling-group');
  const aec = document.getElementById('aec');
  const exposure = document.getElementById('aec_value-group');
  const awb = document.getElementById('awb_gain');
  const wb = document.getElementById('wb_mode-group');
  const framesize = document.getElementById('framesize');

  let url_base = document.location.origin;

  let is_streaming = false;

  function id_generate()
  {
    return Date.now().toString(16).padStart(12, "0") + parseInt(Math.random() * 100000000, 10).toString(16).padStart(8, "0");
  }

  function query_send(query)
  {
    fetch(query).then(
      function(response)
      {
        console.log(`request to ${query} finished, status: ${response.status}`);
      }
    );
  }

  function config_update(el)
  {
    let value = 0;

    switch (el.type)
    {
      case 'checkbox':
        value = el.checked ? 1 : 0;
        break;
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

    query_send(`${url_base}/control?var=${el.id}&val=${value}`);
  }

  function element_set_visible(el, value)
  {
    if (value)
    {
      el.classList.remove('hidden');
    }
    else
    {
      el.classList.add('hidden');
    }
  }

  function element_value_update(el, value)
  {
    let initialValue;

    if (el.type === 'checkbox')
    {
      initialValue = el.checked;
      value = !!value;
      el.checked = value;
    }
    else
    {
      initialValue = el.value;
      el.value = value;
    }

    if (el.id === "aec")
    {
      element_set_visible(exposure, value);
    }
    else if (el.id === "agc")
    {
      if (cam_type == "ov2460")
      {
        element_set_visible(gainCeiling, value);
      }

      element_set_visible(agcGain, !value);
    }
    else if (el.id === "awb_gain")
    {
      element_set_visible(wb, value);
    }
  }

  function status_update()
  {
    fetch(`${url_base}/status`).then(
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
            element_value_update(el, state[el.id]);
          }
	);
      }
    );
  }

  function stream_stop()
  {
    window.stop();
    streamButton.innerHTML = 'Start Stream';
    is_streaming = false;
  }

  function stream_start()
  {
    view.setAttribute("src", url_base + "/stream?id=" + id_generate());
    element_set_visible(viewContainer, true);
    streamButton.innerHTML = 'Stop Stream';
    is_streaming = true;
  }

  resetButton.onclick = () =>
  {
    if (is_streaming)
    {
      stream_stop();
    }

    query_send(`${url_base}/reset`);

    status_update();
  };

  stillButton.onclick = () =>
  {
    if (is_streaming)
    {
      stream_stop();
    }
    else
    {
      view.setAttribute("src", url_base + "/capture?id=" + id_generate());
      element_set_visible(viewContainer, true);
    }
  };

  closeButton.onclick = () =>
  {
    stream_stop();
    element_set_visible(viewContainer, false);
  };

  streamButton.onclick = () =>
  {
    if (is_streaming)
    {
      stream_stop()
    }
    else
    {
      stream_start()
    }
  };

  // Attach default on change action

  document.querySelectorAll('.default-action').forEach(
    el =>
    {
      el.onchange = () => config_update(el);
    }
  );

  // Gain

  agc.onchange = () =>
  {
    config_update(agc);
    if (cam_type == "ov2460")
    {
      element_set_visible(gainCeiling, agc.checked);
    }
    element_set_visible(agcGain, !agc.checked);
  }

  // Exposure

  aec.onchange = () =>
  {
    config_update(aec);
    element_set_visible(exposure, !aec.checked);
  }

  // AWB

  awb.onchange = () =>
  {
    config_update(awb);
    element_set_visible(web, awb.checked);
  }

  // framesize

  framesize.onchange = () =>
  {
    config_update(framesize);
  };

  status_update();
}

document.addEventListener('DOMContentLoaded', onContentLoaded);
