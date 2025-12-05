<?php
// Lire le contenu du fichier JSON
$jsonData = file_get_contents('data.json');

// Décoder le JSON en tableau associatif
$data = json_decode($jsonData, true);

// Vérifier si le décodage a fonctionné
if ($data === null) {
    die("Erreur : impossible de lire le fichier JSON.");
}

// Extraire les dernières valeurs
$temperature = end($data['temperature'])['value'] ?? 'N/A';
$humidite = end($data['humidity'])['value'] ?? 'N/A';
$date = end($data['temperature'])['timestamp'] ?? 'N/A';
?>
<!DOCTYPE html>
<html lang="fr">
<head>
    <meta charset="UTF-8">
    <title>Température et Humidité</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background: #f3f4f6;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            height: 100vh;
        }
        .card {
            background: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 4px 10px rgba(0,0,0,0.1);
            text-align: center;
        }
        h1 {
            margin-bottom: 20px;
        }
        .value {
            font-size: 2em;
            color: #2563eb;
        }
    </style>
</head>
<body>
    <div class="card">
        <h1>Données Météo</h1>
        <p>🌡️ Température : <span class="value"><?= $temperature ?> °C</span></p>
        <p>💧 Humidité : <span class="value"><?= $humidite ?> %</span></p>
        <p>📅 Dernière mise à jour : <?= $date ?></p>
    </div>
</body>
</html>

