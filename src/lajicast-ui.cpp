/*
 * LajiCast OBS plugin UI and local MediaMTX controller
 * Copyright (C) 2026 ProfeCésar
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.h>

#include <QDateTime>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QTcpServer>
#include <QTcpSocket>
#include <QProcess>
#include <QWidget>

#include <memory>

namespace {

QString moduleFile(const char *name)
{
	char *path = obs_module_file(name);
	if (!path)
		return {};
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

QString lanAddress()
{
	QString fallback = QStringLiteral("127.0.0.1");
	QString preferred;
	for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
		if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning) ||
		    (iface.flags() & QNetworkInterface::IsLoopBack))
			continue;

		for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
			const QHostAddress address = entry.ip();
			if (address.protocol() != QAbstractSocket::IPv4Protocol)
				continue;
			const QString ip = address.toString();
			if (ip.startsWith(QStringLiteral("169.254.")))
				continue;
			if (ip.startsWith(QStringLiteral("192.168.")) || ip.startsWith(QStringLiteral("10.")) ||
			    ip.startsWith(QStringLiteral("172.")))
				return ip;
			if (preferred.isEmpty())
				preferred = ip;
		}
	}
	return preferred.isEmpty() ? fallback : preferred;
}

QString appDataDirectory()
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
		.filePath(QStringLiteral("LajiCast"));
}

class LajiCastPanel final : public QWidget {
public:
	explicit LajiCastPanel(QWidget *parent = nullptr) : QWidget(parent)
	{
		setWindowTitle(QStringLiteral("LajiCast"));
		setMinimumWidth(410);

		auto *layout = new QVBoxLayout(this);
		auto *heading = new QLabel(QStringLiteral("LajiCast · Televisión escolar"), this);
		QFont headingFont = heading->font();
		headingFont.setPointSize(15);
		headingFont.setBold(true);
		heading->setFont(headingFont);
		layout->addWidget(heading);

		statusLabel = new QLabel(QStringLiteral("MediaMTX aún no está configurado"), this);
		statusLabel->setWordWrap(true);
		layout->addWidget(statusLabel);

		auto *pathRow = new QHBoxLayout();
		mediaPath = new QLineEdit(this);
		mediaPath->setPlaceholderText(QStringLiteral("Selecciona mediamtx.exe"));
		browseButton = new QPushButton(QStringLiteral("Buscar…"), this);
		pathRow->addWidget(mediaPath, 1);
		pathRow->addWidget(browseButton);
		layout->addLayout(pathRow);

		auto *buttonRow = new QHBoxLayout();
		startButton = new QPushButton(QStringLiteral("Iniciar LajiCast"), this);
		stopButton = new QPushButton(QStringLiteral("Detener"), this);
		stopButton->setEnabled(false);
		buttonRow->addWidget(startButton);
		buttonRow->addWidget(stopButton);
		layout->addLayout(buttonRow);

		viewerUrl = new QLineEdit(this);
		viewerUrl->setReadOnly(true);
		viewerUrl->setPlaceholderText(QStringLiteral("El enlace para las aulas aparecerá aquí"));
		layout->addWidget(new QLabel(QStringLiteral("Enlace para las aulas"), this));
		layout->addWidget(viewerUrl);
		copyButton = new QPushButton(QStringLiteral("Copiar enlace"), this);
		copyButton->setEnabled(false);
		layout->addWidget(copyButton);

		layout->addWidget(new QLabel(QStringLiteral("Aulas conectadas"), this));
		clients = new QTableWidget(0, 3, this);
		clients->setHorizontalHeaderLabels({QStringLiteral("Aula"), QStringLiteral("Dirección"), QStringLiteral("Conectada desde")});
		clients->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		clients->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
		clients->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
		clients->setEditTriggers(QAbstractItemView::NoEditTriggers);
		clients->setSelectionMode(QAbstractItemView::NoSelection);
		clients->setMinimumHeight(140);
		layout->addWidget(clients, 1);

		auto *help = new QLabel(QStringLiteral("En el aula se abre el enlace y se escribe el nombre del aula. "
						       "No requiere instalar un complemento de OBS en los clientes."),
				       this);
		help->setWordWrap(true);
		layout->addWidget(help);

		settings = std::make_unique<QSettings>(QStringLiteral("CEIP La Lajita"), QStringLiteral("LajiCast"));
		mediaPath->setText(settings->value(QStringLiteral("mediamtxPath"), moduleFile("mediamtx.exe")).toString());

		manager = new QNetworkAccessManager(this);
		pollTimer = new QTimer(this);
		pollTimer->setInterval(1500);
		connect(pollTimer, &QTimer::timeout, this, [this] { pollServer(); });
		connect(browseButton, &QPushButton::clicked, this, [this] { chooseMediaMTX(); });
		connect(startButton, &QPushButton::clicked, this, [this] { startAll(); });
		connect(stopButton, &QPushButton::clicked, this, [this] { stopAll(); });
		connect(copyButton, &QPushButton::clicked, this, [this] {
			QApplication::clipboard()->setText(viewerUrl->text());
		});

		server.setProcessChannelMode(QProcess::MergedChannels);
		connect(&server, &QProcess::readyReadStandardOutput, this, [this] {
			const QByteArray output = server.readAllStandardOutput();
			if (!output.isEmpty())
				blog(LOG_INFO, "[LajiCast/MediaMTX] %s", output.trimmed().constData());
		});
		connect(&server, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
			setStatus(QStringLiteral("No se pudo iniciar MediaMTX: %1").arg(server.errorString()));
		});
		connect(&server, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
			[this](int code, QProcess::ExitStatus) {
				if (wanted) {
					setStatus(QStringLiteral("MediaMTX se cerró (código %1). Reintentando…").arg(code));
					QTimer::singleShot(3000, this, [this] { restartServerIfNeeded(); });
				} else {
					setStatus(QStringLiteral("LajiCast detenido"));
				}
			});

		outputRetryTimer = new QTimer(this);
		outputRetryTimer->setInterval(2000);
		connect(outputRetryTimer, &QTimer::timeout, this, [this] { tryStartOutput(); });
	}

	~LajiCastPanel() override { shutdownBackend(); }

	void startAll()
	{
		const QString exe = mediaPath->text().trimmed();
		if (exe.isEmpty() || !QFile::exists(exe)) {
			setStatus(QStringLiteral("Selecciona el archivo oficial mediamtx.exe para el ordenador central."));
			return;
		}
		if (!viewerServer.listen(QHostAddress::AnyIPv4, 8000)) {
			setStatus(QStringLiteral("No se pudo abrir el puerto 8000 para la página de las aulas: %1")
					  .arg(viewerServer.errorString()));
			return;
		}
		connect(&viewerServer, &QTcpServer::newConnection, this, [this] { serveViewerRequests(); });

		if (!writeServerConfig()) {
			viewerServer.close();
			return;
		}

		settings->setValue(QStringLiteral("mediamtxPath"), exe);
		server.setProgram(exe);
		server.setArguments({configPath});
		server.start();
		if (!server.waitForStarted(3000)) {
			viewerServer.close();
			setStatus(QStringLiteral("MediaMTX no arrancó: %1").arg(server.errorString()));
			return;
		}

		wanted = true;
		startButton->setEnabled(false);
		stopButton->setEnabled(true);
		mediaPath->setEnabled(false);
		viewerUrl->setText(QStringLiteral("http://%1:8000/").arg(lanAddress()));
		copyButton->setEnabled(true);
		setStatus(QStringLiteral("Iniciando servidor y preparando la emisión…"));
		pollTimer->start();
		outputRetryTimer->start();
	}

	void stopAll()
	{
		shutdownBackend();
		clients->setRowCount(0);
		viewerUrl->clear();
		copyButton->setEnabled(false);
		startButton->setEnabled(true);
		stopButton->setEnabled(false);
		mediaPath->setEnabled(true);
		setStatus(QStringLiteral("LajiCast detenido"));
	}

	void shutdownBackend()
	{
		wanted = false;
		serverReady = false;
		pollTimer->stop();
		outputRetryTimer->stop();
		if (output && obs_output_active(output))
			obs_output_stop(output);
		releaseOutput();
		if (server.state() != QProcess::NotRunning) {
			server.terminate();
			if (!server.waitForFinished(2000)) {
				server.kill();
				server.waitForFinished(1000);
			}
		}
		viewerServer.close();
	}

private:
	void setStatus(const QString &text) { statusLabel->setText(text); }

	void chooseMediaMTX()
	{
		const QString picked = QFileDialog::getOpenFileName(this, QStringLiteral("Seleccionar MediaMTX"),
											 mediaPath->text(),
											 QStringLiteral("MediaMTX (mediamtx.exe);;Ejecutable (*.exe)"));
		if (!picked.isEmpty())
			mediaPath->setText(picked);
	}

	bool writeServerConfig()
	{
		QDir().mkpath(appDataDirectory());
		configPath = QDir(appDataDirectory()).filePath(QStringLiteral("mediamtx.yml"));
		const QString ip = lanAddress();
		const QString yaml = QStringLiteral(
			"logLevel: info\n"
			"logDestinations: [stdout]\n"
			"api: true\n"
			"apiAddress: 127.0.0.1:9997\n"
			"webrtc: true\n"
			"webrtcAddress: :8889\n"
			"webrtcLocalUDPAddress: :8189\n"
			"webrtcAdditionalHosts: ['%1']\n"
			"webrtcAllowOrigins: ['http://%1:8000']\n"
			"paths:\n"
			"  lajicast: {}\n").arg(ip);
		QSaveFile file(configPath);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(yaml.toUtf8()) < 0 || !file.commit()) {
			setStatus(QStringLiteral("No se pudo crear la configuración de MediaMTX: %1").arg(file.errorString()));
			return false;
		}
		return true;
	}

	void restartServerIfNeeded()
	{
		if (!wanted || server.state() != QProcess::NotRunning)
			return;
		server.start();
	}

	void pollServer()
	{
		if (!wanted || server.state() == QProcess::NotRunning)
			return;
		if (apiRequestActive)
			return;
		apiRequestActive = true;
		QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:9997/v3/info")));
		QNetworkReply *reply = manager->get(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply] {
			apiRequestActive = false;
			const bool ok = reply->error() == QNetworkReply::NoError;
			reply->deleteLater();
			if (!ok) {
				setStatus(QStringLiteral("MediaMTX está iniciando…"));
				return;
			}
			serverReady = true;
			if (!output) {
				if (!createOutput())
					return;
			}
			refreshClients();
		});
	}

	bool createOutput()
	{
		obs_data_t *serviceSettings = obs_data_create();
		obs_data_set_string(serviceSettings, "server", "http://127.0.0.1:8889/lajicast/whip");
		obs_service_t *newService = obs_service_create("whip_custom", "LajiCast WebRTC", serviceSettings, nullptr);
		obs_data_release(serviceSettings);
		if (!newService) {
			setStatus(QStringLiteral("OBS no ha registrado el servicio WHIP. Comprueba que el módulo WebRTC de OBS está habilitado."));
			return false;
		}

		obs_output_t *newOutput = obs_output_create("whip_output", "LajiCast WebRTC", nullptr, nullptr);
		if (!newOutput) {
			obs_service_release(newService);
			setStatus(QStringLiteral("OBS no ha registrado la salida WHIP. Comprueba el módulo WebRTC de OBS."));
			return false;
		}

		obs_data_t *videoSettings = obs_data_create();
		obs_data_set_int(videoSettings, "bitrate", 3000);
		obs_data_set_int(videoSettings, "keyint_sec", 2);
		obs_data_set_string(videoSettings, "preset", "veryfast");
		obs_data_set_string(videoSettings, "rate_control", "CBR");
		obs_data_t *audioSettings = obs_data_create();
		obs_data_set_int(audioSettings, "bitrate", 128);
		obs_service_apply_encoder_settings(newService, videoSettings, audioSettings);

		obs_encoder_t *videoEncoder = obs_video_encoder_create("obs_x264", "LajiCast Video", videoSettings, nullptr);
		obs_encoder_t *audioEncoder = obs_audio_encoder_create("ffmpeg_opus", "LajiCast Audio", audioSettings, 0, nullptr);
		obs_data_release(videoSettings);
		obs_data_release(audioSettings);
		if (!videoEncoder || !audioEncoder) {
			if (videoEncoder)
				obs_encoder_release(videoEncoder);
			if (audioEncoder)
				obs_encoder_release(audioEncoder);
			obs_output_release(newOutput);
			obs_service_release(newService);
			setStatus(QStringLiteral("Falta el codificador H.264 o Opus en OBS. Comprueba los módulos obs-x264 y obs-ffmpeg."));
			return false;
		}

		obs_encoder_set_video(videoEncoder, obs_get_video());
		obs_encoder_set_audio(audioEncoder, obs_get_audio());
		obs_output_set_video_encoder(newOutput, videoEncoder);
		obs_output_set_audio_encoder(newOutput, audioEncoder, 0);
		obs_output_set_service(newOutput, newService);
		obs_output_set_reconnect_settings(newOutput, 0, 0);
		obs_encoder_release(videoEncoder);
		obs_encoder_release(audioEncoder);
		service = newService;
		output = newOutput;
		lastOutputAttempt = QDateTime::currentDateTime().addSecs(-10);
		return true;
	}

	void tryStartOutput()
	{
		if (!wanted || !serverReady || !output || obs_output_active(output))
			return;
		if (lastOutputAttempt.secsTo(QDateTime::currentDateTime()) < 5)
			return;
		lastOutputAttempt = QDateTime::currentDateTime();
		if (obs_output_start(output))
			setStatus(QStringLiteral("Conectando la emisión de OBS a MediaMTX…"));
		else
			setStatus(QStringLiteral("No se pudo iniciar la salida WHIP. Se volverá a intentar."));
	}

	void refreshClients()
	{
		// The legacy route is retained by MediaMTX and covers the 1.21 baseline.
		QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:9997/v3/webrtcsessions/list")));
		QNetworkReply *reply = manager->get(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply] {
			if (reply->error() == QNetworkReply::NoError) {
				const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
				const QJsonArray sessions = document.object().value(QStringLiteral("items")).toArray();
				clients->setRowCount(0);
				for (const QJsonValue &value : sessions) {
					const QJsonObject session = value.toObject();
					if (session.value(QStringLiteral("state")).toString() != QStringLiteral("read"))
						continue;
					QUrlQuery query(session.value(QStringLiteral("query")).toString());
					QString classroom = query.queryItemValue(QStringLiteral("classroom"));
					if (classroom.isEmpty())
						classroom = QStringLiteral("Aula sin nombre");
					const int row = clients->rowCount();
					clients->insertRow(row);
					clients->setItem(row, 0, new QTableWidgetItem(classroom));
					clients->setItem(row, 1, new QTableWidgetItem(session.value(QStringLiteral("remoteAddr")).toString()));
					const QDateTime created = QDateTime::fromString(session.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs);
					clients->setItem(row, 2, new QTableWidgetItem(created.isValid() ? created.toLocalTime().toString(QStringLiteral("HH:mm:ss")) : QStringLiteral("—")));
				}
				if (obs_output_active(output))
					setStatus(QStringLiteral("Emitiendo por WebRTC · %1 aula(s) conectada(s)").arg(clients->rowCount()));
			}
		reply->deleteLater();
		});
	}

	void serveViewerRequests()
	{
		while (viewerServer.hasPendingConnections()) {
			QTcpSocket *socket = viewerServer.nextPendingConnection();
			connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
				const QByteArray request = socket->readAll();
				if (request.isEmpty())
					return;
				const QByteArray requestLine = request.left(request.indexOf("\r\n"));
				const QList<QByteArray> requestParts = requestLine.split(' ');
				const QByteArray requestPath = requestParts.size() > 1 ? requestParts.at(1).split('?').first() : QByteArray("/");
				QString filename = QStringLiteral("viewer.html");
				QByteArray contentType = "text/html; charset=utf-8";
				if (requestPath == "/branding/lajicast.png") {
					filename = QStringLiteral("branding/lajicast.png");
					contentType = "image/png";
				}
				QFile file(moduleFile(filename.toUtf8().constData()));
				QByteArray body;
				int statusCode = 200;
				QByteArray reason = "OK";
				if (requestPath != "/" && requestPath != "/branding/lajicast.png") {
					statusCode = 404;
					reason = "Not Found";
				}
				if (statusCode == 200 && file.open(QIODevice::ReadOnly))
					body = file.readAll();
				else if (statusCode == 200) {
					statusCode = 500;
					reason = "Internal Server Error";
					body = "No se encontró el recurso de LajiCast.";
				}
				QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reason + "\r\n";
				response += "Content-Type: " + contentType + "\r\n";
				response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
				response += "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
				response += body;
				socket->write(response);
				socket->disconnectFromHost();
			});
			connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
		}
	}

	void releaseOutput()
	{
		if (output) {
			obs_output_release(output);
			output = nullptr;
		}
		if (service) {
			obs_service_release(service);
			service = nullptr;
		}
	}

	QLineEdit *mediaPath = nullptr;
	QLabel *statusLabel = nullptr;
	QLineEdit *viewerUrl = nullptr;
	QPushButton *browseButton = nullptr;
	QPushButton *startButton = nullptr;
	QPushButton *stopButton = nullptr;
	QPushButton *copyButton = nullptr;
	QTableWidget *clients = nullptr;
	QNetworkAccessManager *manager = nullptr;
	QTimer *pollTimer = nullptr;
	QTimer *outputRetryTimer = nullptr;
	QProcess server;
	QTcpServer viewerServer;
	std::unique_ptr<QSettings> settings;
	QString configPath;
	QDateTime lastOutputAttempt;
	obs_output_t *output = nullptr;
	obs_service_t *service = nullptr;
	bool wanted = false;
	bool serverReady = false;
	bool apiRequestActive = false;
};

LajiCastPanel *panel = nullptr;

} // namespace

extern "C" bool lajicast_ui_load(void)
{
	if (panel)
		return true;
	panel = new LajiCastPanel(static_cast<QWidget *>(obs_frontend_get_main_window()));
	if (!obs_frontend_add_dock_by_id("lajicast-dock", "LajiCast", panel)) {
		delete panel;
		panel = nullptr;
		blog(LOG_ERROR, "[LajiCast] No se pudo crear el panel de OBS");
		return false;
	}
	return true;
}

extern "C" void lajicast_ui_unload(void)
{
	if (!panel)
		return;
	obs_frontend_remove_dock("lajicast-dock");
	delete panel;
	panel = nullptr;
}
