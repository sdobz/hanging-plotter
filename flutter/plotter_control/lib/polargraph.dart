import 'dart:ffi';
import 'dart:ui';

import 'package:flutter_blue/flutter_blue.dart';

const String polargraphUUID = "000000ff-0000-1000-8000-00805f9b34fb";
final Guid polargraphGuid = Guid(polargraphUUID);

bool isPolargraph(ScanResult d) =>
    d.advertisementData.serviceUuids.isNotEmpty &&
    d.advertisementData.serviceUuids.contains(polargraphUUID);

class Polargraph {
  Polargraph({this.device});
  final BluetoothDevice device;
  BluetoothCharacteristic velocityCharacteristic;

  void discover() {
    device.discoverServices().then((services) {
      services.forEach((service) {
        if (service.uuid == polargraphGuid) {
          service.characteristics.forEach((characteristic) {
            print(characteristic.serviceUuid);
          });
        }
      });
    });
  }

  void sendVelocity(Offset vel) {
    int vx = (vel.dx * 128).round().clamp(-127, 128);
    int vy = (vel.dy * 128).round().clamp(-127, 128);
    print(vx);
    print(vy);
  }
}
