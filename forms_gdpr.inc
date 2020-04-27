<?php

function forms_gdpr_export($string)
{
	if (empty($string)) {
		http_response_code(400);
		exit;
	}

	parse_str($string, $arr);
	$email = $arr['email'] ?? null;
	$pn = $arr['pn'] ?? null;
	$ticket = getallheaders()['Authorization'] ?? $arr['ticket'];

	$pn = str_replace('-', '', $pn);

	$emailcid = 10;
	$pncid = 11;

	if ((!$email && !$pn) || !$ticket) {
		http_response_code(204);
	}

	$ch = curl_init();
	$apiurl = 'https://toker-test.dsv.su.se/verify';

	curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
	curl_setopt($ch, CURLOPT_TIMEOUT, 3);
	curl_setopt($ch, CURLOPT_HTTPHEADER, array('Accept: application/json'));
	curl_setopt($ch, CURLOPT_POSTFIELDS, $ticket);
	curl_setopt($ch, CURLOPT_URL, $apiurl);
	$contents = curl_exec($ch);
	$headers  = curl_getinfo($ch);
	curl_close($ch);

	// Check auth.
	if ($headers['http_code'] !== 200 || !in_array('urn:mace:swami.se:gmai:dsv-user:gdpr', json_decode($contents)->entitlements)) {
		 Throw unauthorized code.
			http_response_code(401);
			die();
	}

	$path = DRUPAL_ROOT . '/sites/default/files/tmp/';
	delTree($path);
	mkdir($path, 0777);

	$webforms = node_load_multiple(array(), array('type' => 'webform'));

	$allempty = true;

	module_load_include('inc', 'webform', 'includes/webform.submissions');

	foreach ($webforms as $webform) {
		$type = $webform->title;
		$submissions = webform_get_submissions(array('nid' => $webform->nid));
		$submissions_email = $email ? array_filter($submissions, function ($n) use ($email, $emailcid) {
			return (trim($n->data[$emailcid][0]) == $email);
		}) : array();
		$submissions_pn = $pn ? array_filter($submissions, function ($n) use ($pn, $pncid) {
			$spn = trim(str_replace('-', '', $n->data[$pncid][0]));
			return ($spn == $pn || substr($spn, 2) == $pn);
		}) : array();

		if ($email && $pn && (array_values($submissions_email) !== array_values($submissions_pn))) {
			// The requested user could not be found or credentials point to different submissions.
			http_response_code(204);
			die();
		}

		$submissions = $submissions_email + $submissions_pn;

		if (empty($submissions)) {
			continue;
		}

		$allempty = false;

		$id = $email ?? $pn;

		$headers = array();
		$rows = array();
		$files = array();

		foreach ($submissions as $s) {
			$webform = node_load($s->nid);
			foreach ($s->data as $key => $value) {
				$field = $webform->webform['components'][$key];
				$headers[] = $field['name'];
				$rows[$s->$sid][] = export_webform($value, $field, $files);
			}
		}

		$file = fopen($path . $id . '-' . $type . '.csv', 'w') or die('Problems');

		fputcsv($file, $headers);
		foreach ($rows as $row) {
			fputcsv($file, $row);
		}
		fclose($file);
	}

	if ($allempty) {
		http_response_code(204);
		exit;
	}

	$zip = new ZipArchive;
	if ($zip->open($path . $id . '.zip', ZipArchive::CREATE) === TRUE) {
		foreach ($types as $type) {
			$file = $path . $id . '-' . $type . '.csv';
			$new_filename = substr($file, strrpos($file, '/') + 1);
			$zip->addFile($file, $new_filename);
		}
		foreach ($files as $file) {
			$file = str_replace('forms/gdpr/', 'forms/', $file);
			$new_filename = substr($file, strrpos($file, '/') + 1);
			$current = file_get_contents($file);
			if (!empty($current)) {
				file_put_contents($path . '/' . $new_filename, $current);
			}
			$zip->addFile($path . '/' . $new_filename, $new_filename);
		}
		$zip->close();
		readfile($path . $id . '.zip');
		http_response_code(200);
	} else {
		http_response_code(500);
		die();
	}
}

// Functions

function delTree($dir)
{
	$files = array_diff(scandir($dir), array('.', '..'));
	foreach ($files as $file) {
		(is_dir("$dir/$file")) ? delTree("$dir/$file") : unlink("$dir/$file");
	}
	return rmdir($dir);
}

function export_webform($value, $field, &$files)
{
	switch ($field['type']) {
		case 'textfield':
		case 'email':
			return $value[0];
			break;
		case 'select':
			$options = explode('|', $field['extra']['items']);
			$key = array_search($value, $options);
			return ($options[$key + 1]);
			break;
		case 'file':
			$file = file_load($value[0]);
			$url = file_create_url($file->uri);
			$files[] = $url;
			return $file->filename;
			break;
		default:
			if ($value[0]) {
				return $value[0];
			}
			break;
	}
}
