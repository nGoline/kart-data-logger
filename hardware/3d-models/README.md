# 3D Models

This folder contains the Fusion 360 source models and assembly exports used to validate the kart display enclosure and its subcomponents.

## Included files

* `Display.f3d` — main display enclosure model.
* `Display Assembly.3mf` — assembly export showing how the display, rear cover, battery support, and wheel holder fit together.
* `Battery.f3d` — battery pack model and holder details.
* `4056 Battery Charger.f3d` — USB-C charging board model and placement.
* `GPS.f3d` — GPS enclosure model.
* `MP6050.f3d` — IMU module model.

## Design intent

The primary part is `Display.f3d`. The other models were created to build the assembly and verify that all pieces fit correctly.

## Mounting and fastening

* The display board is held in place by small pegs that fit into matching bosses in the enclosure.
* The back cover is pressed onto the display body by rear cover pegs and then bolted in place.
* The back cover uses eight `M3X3X5` heat inserts and eight `M3x6` bolts.
* The helmet GPS enclosure uses four `M2.5x6mm` bolts and four `M2.5x4x4mm` heat inserts.
* The GPS holder fits the ATGM336 module and a `25x25mm` antenna.

## Battery and charging

* The rear cover supports a `LGBH4ZB4857` 2000mAh battery.
* The battery is sourced from a Dell Latitude 5490 battery pack, which contains three of these cells.
* A `4056` 1A USB-C charging board is integrated into the back cover space.

## Wheel holder and battery cover

* The battery cover / wheel holder assembly is not permanently fixed to the back cover.
* It is held in place by velcro loops that attach from the front of the display case, securing the assembly without rigid fasteners.
* The wheel holder is fixed to the rear plate using four `M3X3X5` heat inserts and four `M3x6` bolts.
* This wheel holder section can be modified for different kart wheel sizes, or multiple sizes can be printed and swapped without redesigning the entire case.

## IMU placement

* The IMU is mounted to the front of the battery back plate using double-sided tape.

## Notes

* Use the Fusion 360 `.f3d` files for source edits and design updates.
* The `.3mf` assembly file is useful for fit verification and export to compatible printing workflows.
* Confirm fastener lengths and insert positions before printing if you modify the enclosure geometry.
