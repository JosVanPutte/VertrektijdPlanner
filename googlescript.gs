function doGet(e) {
  var resultaat = berekenReis(e.parameter.van, e.parameter.naar);
  
  // Dit zorgt ervoor dat de ESP32 de data als JSON binnenkrijgt
  return ContentService.createTextOutput(JSON.stringify(resultaat))
    .setMimeType(ContentService.MimeType.JSON);
}

function berekenReis(van, naar) {
  var direct = Maps.newDirectionFinder();
  direct.setOrigin(van);
  direct.setDestination(naar);
  direct.setMode(Maps.DirectionFinder.Mode.TRANSIT);
  // Gebruik de huidige tijd als vertrektijd voor de berekening
  direct.setDepart(new Date()); 

  var routes = direct.getDirections();
  
  if (routes.routes && routes.routes.length > 0) {
    var leg = routes["routes"][0]["legs"][0];
    
    // 1. Reistijd berekenen (bestaande logica)
    var secs = leg["duration"]["value"];
    var mins = Math.floor(secs / 60);
    var durationStr = mins + " min"; // Iets korter voor op het OLED scherm

    // 2. Vertrektijd ophalen
    var vertrekStr = "--:--"; // Fallback
    if (leg["departure_time"]) {
      // Gebruik de 'value' (seconden sinds 1970) om een Date object te maken
      // Vermenigvuldig met 1000 omdat JS in milliseconden rekent
      var vertrekDatum = new Date(leg["departure_time"]["value"] * 1000);
      
      // Gebruik "Europe/Amsterdam" in plaats van "GMT+1" voor automatische zomertijd
      vertrekStr = Utilities.formatDate(vertrekDatum, "Europe/Amsterdam", "HH:mm");
    }

    return {
      "status": "success",
      "reistijd": durationStr,
      "vertrek": vertrekStr
    };
  } else {
    return {
      "status": "error",
      "message": "Geen route gevonden"
    };
  }
}
