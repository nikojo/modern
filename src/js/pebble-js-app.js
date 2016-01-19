Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready!');
});

Pebble.addEventListener('showConfiguration', function() {
  var url = 'https://rawgit.com/nikojo/modern/master/config/index.html';
  console.log('Showing configuration page: ' + url);

  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function(e) {
  var configData = JSON.parse(decodeURIComponent(e.response));
  console.log('Configuration page returned: ' + JSON.stringify(configData));
  
  var minuteColor = configData['minute_hand_color'];
  var hourColor = configData['hour_hand_color'];

  var dict = {};
  dict.KEY_MINUTE_COLOR_R = parseInt(minuteColor.substring(2, 4), 16);
  dict.KEY_MINUTE_COLOR_G = parseInt(minuteColor.substring(4, 6), 16);
  dict.KEY_MINUTE_COLOR_B = parseInt(minuteColor.substring(6), 16);
  dict.KEY_HOUR_COLOR_R = parseInt(hourColor.substring(2, 4), 16);
  dict.KEY_HOUR_COLOR_G = parseInt(hourColor.substring(4, 6), 16);
  dict.KEY_HOUR_COLOR_B = parseInt(hourColor.substring(6), 16);
  
  // Send to watchapp
  Pebble.sendAppMessage(dict, function() {
    console.log('Send successful: ' + JSON.stringify(dict));
  }, function() {
    console.log('Send failed!');
  });
  
});
