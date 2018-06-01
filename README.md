# qt-instrument-cluster

The project is part of a Fog Network Simulator which tries to simulate the use case of a vehicle collision on a public road and prevent further incidents by sending alerts to other drivers in the vicinity.

The Instrument cluster is the visualisation part of the simulator which displays internal vehicle parameters which are sent on the CAN Bus network and acts as a head-unit which communicates with a Fog Network, receiving status signals and displays them appropriately.

This is a fork of qtcluster, a public demo of automotive instrument cluster capabilities created with the QT framework:
Source: http://code.qt.io/cgit/qt/qtdoc.git/tree/doc/src/snippets/qtcluster?h=5.10


Features:
- Connection to virtual CAN Bus via SSL sockets:  http://doc.qt.io/qt-5/qsslsocket.html
- Connection to virtual CAN Bus via QCanBus:  http://doc.qt.io/qt-5/qcanbus.html
- Connection to a Fog Node via sockets
- Visualisation of Vehicle CAN Bus parameters
- Visualisation of Collision warning

Built with version 5.10.1
