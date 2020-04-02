import 'dart:math';

import 'package:flutter/material.dart';

import 'package:flutter_blue/flutter_blue.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/scheduler.dart';

class DeviceControlScreen extends StatelessWidget {
  const DeviceControlScreen({Key key, this.device}) : super(key: key);

  final BluetoothDevice device;

  void _onChanged(Offset joystick) {
    //print(joystick.dx);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(device.name),
        actions: <Widget>[
          StreamBuilder<BluetoothDeviceState>(
            stream: device.state,
            initialData: BluetoothDeviceState.connecting,
            builder: (c, snapshot) {
              VoidCallback onPressed;
              String text;
              switch (snapshot.data) {
                case BluetoothDeviceState.connected:
                  onPressed = () => device.disconnect();
                  text = 'DISCONNECT';
                  break;
                case BluetoothDeviceState.disconnected:
                  onPressed = () => device.connect();
                  text = 'CONNECT';
                  break;
                default:
                  onPressed = null;
                  text = snapshot.data.toString().substring(21).toUpperCase();
                  break;
              }
              return FlatButton(
                  onPressed: onPressed,
                  child: Text(
                    text,
                    style: Theme.of(context)
                        .primaryTextTheme
                        .button
                        .copyWith(color: Colors.white),
                  ));
            },
          )
        ],
      ),
      body: SingleChildScrollView(
        child: Column(children: <Widget>[
          StreamBuilder<BluetoothDeviceState>(
            stream: device.state,
            initialData: BluetoothDeviceState.connecting,
            builder: (c, snapshot) => ListTile(
              leading: (snapshot.data == BluetoothDeviceState.connected)
                  ? Icon(Icons.bluetooth_connected)
                  : Icon(Icons.bluetooth_disabled),
              title:
                  Text('Device is ${snapshot.data.toString().split('.')[1]}.'),
              subtitle: Text('${device.id}'),
              trailing: StreamBuilder<bool>(
                stream: device.isDiscoveringServices,
                initialData: false,
                builder: (c, snapshot) => IndexedStack(
                  index: snapshot.data ? 1 : 0,
                  children: <Widget>[
                    IconButton(
                      icon: Icon(Icons.refresh),
                      onPressed: () => device.discoverServices(),
                    ),
                    IconButton(
                      icon: SizedBox(
                        child: CircularProgressIndicator(
                          valueColor: AlwaysStoppedAnimation(Colors.grey),
                        ),
                        width: 18.0,
                        height: 18.0,
                      ),
                      onPressed: null,
                    )
                  ],
                ),
              ),
            ),
          ),
          new JoystickControl(onChanged: _onChanged),
        ]),
      ),
    );
  }
}

class JoystickControl extends StatefulWidget {
  final ValueChanged<Offset> onChanged;

  const JoystickControl({Key key, this.onChanged}) : super(key: key);

  @override
  JoystickControlState createState() => new JoystickControlState();
}

/// Draws a circle at supplied position.
///
class JoystickControlState extends State<JoystickControl> {
  static const markerSize = 0.3;
  static const resetRate = 0.02;

  // Widget state
  Offset position = Offset(0, 0);

  Offset touchOffset = Offset(0, 0);

  // Managing resetting position
  Ticker resetTicker;
  int lastTick;

  double get unitScale => 2.0 / (1 - markerSize);

  Offset offsetToUnit(Offset offset) {
    final RenderBox referenceBox = context.findRenderObject();
    if (referenceBox == null) {
      return Offset(0, 0);
    }
    final Size size = referenceBox.size;
    final Offset local = referenceBox.globalToLocal(offset);

    final uncapped =
        Offset((local.dx / size.width) - 0.5, (local.dy / size.height) - 0.5) *
            unitScale;
    return uncapped / max(1, uncapped.distance);
  }

  Offset unitToOffset(Offset scaledUnit) {
    final RenderBox referenceBox = context.findRenderObject();
    if (referenceBox == null) {
      return Offset(0, 0);
    }
    final unit = (scaledUnit / unitScale);
    final Size size = referenceBox.size;

    return Offset((unit.dx + 0.5) * size.width, (unit.dy + 0.5) * size.height);
  }

  void onChanged(Offset touchPos) {
    final p = offsetToUnit(touchPos);
    if (widget.onChanged != null) widget.onChanged(p);

    setState(() {
      position = p;
    });
  }

  bool hitTestSelf(Offset position) {
    final touchUnit = offsetToUnit(position);
    final unitDistance = (touchUnit - position).distance * unitScale;

    return true;
  }

  void handleEvent(PointerEvent event, BoxHitTestEntry entry) {
    if (event is PointerDownEvent) {
      // ??
    }
  }

  void _handlePanStart(DragStartDetails details) {
    final RenderBox referenceBox = context.findRenderObject();
    final localOffset = referenceBox.globalToLocal(details.globalPosition);
    final positionOffset = unitToOffset(position);
    touchOffset = positionOffset - localOffset;

    onChanged(details.globalPosition + touchOffset);
    if (resetTicker != null) {
      resetTicker.stop();
      resetTicker = null;
    }
  }

  void recenterJoystick(duration) {
    if (position.distanceSquared < resetRate) {
      setState(() {
        position = Offset(0.0, 0.0);
      });
      resetTicker.stop();
    }

    final tickTime = DateTime.now().millisecondsSinceEpoch;

    final timeDelta = tickTime - lastTick;

    setState(() {
      position = position * pow(1 - resetRate, timeDelta);
    });

    lastTick = tickTime;
  }

  void _handlePanEnd(DragEndDetails details) {
    lastTick = DateTime.now().millisecondsSinceEpoch;
    resetTicker = Ticker(recenterJoystick);
    resetTicker.start();
  }

  void _handlePanUpdate(DragUpdateDetails details) {
    onChanged(details.globalPosition + touchOffset);
  }

  @override
  Widget build(BuildContext context) {
    RenderBox ro = context.findRenderObject();
    double markerRadius = ro == null ? 0 : ro.size.width * markerSize / 2;
    return new AspectRatio(
      aspectRatio: 1,
      child: new GestureDetector(
        behavior: HitTestBehavior.opaque,
        onPanStart: _handlePanStart,
        onPanEnd: _handlePanEnd,
        onPanUpdate: _handlePanUpdate,
        child: new CustomPaint(
          size: ro != null ? ro.size : new Size(0, 0),
          painter:
              new TouchControlPainter(unitToOffset(position), markerRadius),
        ),
      ),
    );
  }
}

class TouchControlPainter extends CustomPainter {
  final Offset offset;
  final double markerRadius;
  static final markerPaint = new Paint()
    ..color = Colors.blue[400]
    ..style = PaintingStyle.fill;
  static final backgroundPaint = new Paint()
    ..color = Colors.blue[100]
    ..style = PaintingStyle.fill;

  TouchControlPainter(this.offset, this.markerRadius);

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawCircle(Offset(size.width/2, size.height/2), size.width/2, backgroundPaint);
    canvas.drawCircle(offset, markerRadius, markerPaint);
  }

  @override
  bool shouldRepaint(TouchControlPainter old) =>
      offset.dx != old.offset.dx || offset.dy != old.offset.dy;
}
