import 'dart:ui';

import 'package:flutter_blue/flutter_blue.dart';

Guid fromUUID16(String uuid16) {
  // Matches service_uuid from bluetooth.c
  return Guid("0000$uuid16-0000-1000-8000-00805f9b34fb");
}
final Guid polargraphGuid = fromUUID16("00ff");
final String polargraphUUID = polargraphGuid.toString();
// Matches GATTS_UUID_* from bluetooth.c
final Guid motorVelSetGuid = fromUUID16("ff01");
final Guid motorVelRealGuid = fromUUID16("ff02");
final Guid motorPosGuid = fromUUID16("ff03");


bool isPolargraph(ScanResult d) =>
  d.advertisementData.serviceUuids.isNotEmpty &&
  d.advertisementData.serviceUuids.contains(polargraphUUID);

class Polargraph {
  Polargraph({this.device});
  final BluetoothDevice device;
  BluetoothCharacteristic motorVelSet;
  BluetoothCharacteristic motorVelReal;
  BluetoothCharacteristic motorPos;

  void discover() {
    device.discoverServices().then((services) {
      services.forEach((service) {
        service.characteristics.forEach((characteristic) {
            if (characteristic.uuid == motorVelSetGuid) {
              motorVelSet = characteristic;
            } else if (characteristic.uuid == motorVelRealGuid) {
              motorVelReal = characteristic;
            } else if (characteristic.uuid == motorPosGuid) {
              motorPos = characteristic;
            }
          });
      });
    });
  }

  void sendVelocity(Offset vel) {
    if (motorVelSet == null) {
      return;
    }
    int vx = (vel.dx * 128).round().clamp(-127, 128);
    int vy = (vel.dy * 128).round().clamp(-127, 128);
    
    motorVelSet.write([vx, vy], withoutResponse: true);
  }
}
