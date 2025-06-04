## Instalacion

Primero crea un entorno virtual para Python, usando el comando:

### Linux

```sh
python3 -m venv .venv
source .venv/bin/activate
```

### Windows

```sh
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
python -m venv .venv
.venv\Scripts\activate.bat
```

Y para salir del entorno, usa el comando:

```sh
deactivate
```

## Dependencias

Para instalar las dependencias usa el siguiente comando:

```sh
pip install -r gui/requirements.txt
```

Y para guardar:

```sh
pip freeze > gui/requirements.txt
```

Una vez instalado todo, puedes ejecutar el codigo desde `gui/main.py`.

## Empaquetado

Comando para empaquetar la aplicacion:

```sh
pyinstaller gui/main.py --onefile --add-data "gui/img/openchat_logo.png:img" --add-data "gui/img/icon.png:img" --icon=gui/img/icon.ico --name OpenChat
```

El ejecutable resultado esta en la carpeta dist.