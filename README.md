# LajiCast

Plugin escolar para retransmitir desde OBS Studio a las aulas mediante WebRTC.

La base del proyecto es la plantilla oficial de plugins de OBS. MediaMTX proporciona el transporte WebRTC mediante sus interfaces publicadas. El instalador incluye el complemento, MediaMTX y la página de reproducción para las aulas.

## Alcance

- Emisión desde OBS en el ordenador central, independiente de la salida de OBS habitual.
- Acceso de las aulas desde un navegador.
- Reconexión automática del cliente.
- Lista de aulas conectadas y dirección de red.
- Interfaz en español y marca LajiCast.

## Procedencia y licencias

El código, la interfaz, la configuración, el empaquetado y la documentación se crean en este proyecto desde la plantilla oficial de OBS. Solo se han reutilizado los tres formatos del icono de LajiCast.

La revisión y adaptación escolar se atribuye a **ProfeCésar**. Se conserva la licencia GPL-2.0 de la plantilla oficial de OBS. El paquete incluye MediaMTX 1.21.4 y conserva su licencia MIT en `data/MEDIAMTX-LICENSE`.

## Compatibilidad objetivo

- Windows 11 x64
- OBS Studio 33.0.3
- MediaMTX 1.21.4 incluido en el instalador

## Estado de la versión 0.1.0

La versión inicial está operativa y se ha comprobado en Windows 11 con OBS Studio 33.0.3. La prueba confirmó la emisión WebRTC con vídeo y audio, la reproducción desde el navegador y la lista de aulas conectadas.

Incluye reconexión automática del cliente, panel de emisión en OBS, consulta de sesiones WebRTC y un instalador gráfico para Windows. La plantilla de desarrollo usa el SDK de OBS 31.1.1; el complemento se ha compilado y probado con OBS Studio 33.0.3.

## Compilar el instalador

El proyecto está orientado a Windows x64. Para compilarlo desde el código fuente se necesita Visual Studio 2022 con las herramientas de C++ y CMake, además de las dependencias de desarrollo de OBS. El archivo `installer/` contiene el código fuente del instalador; sus archivos compilados y las dependencias locales no se incluyen en el repositorio.
