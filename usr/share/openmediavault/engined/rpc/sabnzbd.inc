<?php

/**
 * Copyright (C) 2013-2015 OpenMediaVault Plugin Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

require_once "openmediavault/config.inc";
require_once "openmediavault/error.inc";
require_once "openmediavault/notify.inc";
require_once "openmediavault/object.inc";
require_once "openmediavault/product.inc";
require_once "openmediavault/rpcservice.inc";
require_once "openmediavault/system.inc";
require_once "openmediavault/util.inc";

class OMVRpcServiceSabnzbd extends OMVRpcServiceAbstract
{
    private $sabnzbdSettingsDir = "/var/opt/sabnzbd/";
    private $sabnzbdSettingsFile = "/var/opt/sabnzbd/sabnzbd.ini";

    private $sabnzbd2SettingsDir = "/var/opt/sabnzbd2/";
    private $sabnzbd2SettingsFile = "/var/opt/sabnzbd2/config.ini";

    private function getPluginName()
    {
        return strtolower($this->getName());
    }

    private function getEventMessagePath()
    {
        return sprintf("org.openmediavault.service.%s", $this->getPluginName());
    }

    private function getXpath()
    {
        return sprintf("//services/%s", $this->getPluginName());
    }

    public function getName()
    {
        return "Sabnzbd";
    }

    public function initialize()
    {
        $this->registerMethod("getSettings");
        $this->registerMethod("setSettings");
        $this->registerMethod("doUpdateSAB");
        $this->registerMethod("doBackup");
        $this->registerMethod("doRestore");
    }

    public function getSettings($params, $context)
    {
        $cmd = sprintf("omv-mkconf %s getsettings", $this->getPluginName());

        if (0 !== $this->exec($cmd, $output)) {
            throw new OMVException(
                OMVErrorMsg::E_EXEC_FAILED,
                $cmd,
                implode("\n", $output)
            );
        }

        global $xmlConfig;

        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        $object = $xmlConfig->get($this->getXpath());

        if (is_null($object)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED,
                $this->getXpath()
            );
        }

        $port = 8080;
        $sslIsEnabled = 0;

        // Get current port as set in Sabnzbd config file
        if (file_exists($this->sabnzbdSettingsFile)) {
            $config = parse_ini_file(
                $this->sabnzbdSettingsFile,
                true,
                INI_SCANNER_RAW
            );

            $sslIsEnabled = isset($config["misc"]["enable_https"]) ? $config["misc"]["enable_https"] : '0';

            if ($sslIsEnabled) {
                if (!empty($config["misc"]["https_port"])) {
                    $port = $config["misc"]["https_port"];
                }
            } else {
                if (!empty($config["misc"]["port"])) {
                    $port = $config["misc"]["port"];
                }
            }
        }

        $object["enable"]         = boolval($object["enable"]);
        $object["showtab"]        = boolval($object["showtab"]);
        $object["newinstance"]    = boolval($object["newinstance"]);
        $object["newinstenable"]  = boolval($object["newinstenable"]);
        $object["ssl"]            = boolval($object["ssl"]);
        $object["ppass"]          = boolval($object["ppass"]);
        $object["update"]         = boolval($object["update"]);
        $object["port"]           = intval($port);

        return $object;
    }

    public function setSettings($params, $context)
    {
        global $xmlConfig;

        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        $this->validateMethodParams(
            $params,
            '{
                "type"       : "object",
                "properties" : {
                    "enable"        : { "type" : "boolean" },
                    "showtab"       : { "type" : "boolean" },
                    "newinstance"   : { "type" : "boolean" },
                    "newinstenable" : { "type" : "boolean" },
                    "ppass"         : { "type" : "boolean" },
                    "ssl"           : { "type" : "boolean" },
                    "update"        : { "type" : "boolean" },
                    "port"          : { "type" : "integer" }
                }
            }'
        );


        // Get existing configuration object
        $oldObject = $xmlConfig->get($this->getXpath());

        if (is_null($oldObject)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED,
                $this->getXpath()
            );
        }

        // Run checks to verify sellected settings
        if ($params["ssl"] == "1") {
            if ($params["ppass"] == "1") {
                throw new OMVException(
                    OMVErrorMsg::E_EXEC_MISC,
                    "You can not enable both HTTPS and Proxy Pass."
                );
            }
            $xSsl = $xmlConfig->get("//webadmin/enablessl");
            if ($xSsl == "1") {
                throw new OMVException(
                    OMVErrorMsg::E_EXEC_MISC,
                    "You have SSL enabled for the main UI, Do NOT enable it here as well."
                );
            }
        }

        if ($params["showtab"] == true && $params["ppass"] != true) {
            if (! file_exists('/var/www/openmediavault/js/omv/module/admin/service/media/Media.js')) {
                throw new OMVException(
                    OMVErrorMsg::E_MISC_FAILURE,
                    "Install links and add an entry or enable Proxy Pass to use the web tab."
                );
            }
        }
        //Checks finished.

        $object = array(
            "enable"        => array_boolval($params, "enable"),
            "showtab"       => array_boolval($params, "showtab"),
            "newinstance"   => array_boolval($params, "newinstance"),
            "newinstenable" => array_boolval($params, "newinstenable"),
            "ppass"         => array_boolval($params, "ppass"),
            "ssl"           => array_boolval($params, "ssl"),
            "update"        => array_boolval($params, "update"),
            "port"          => $params["port"],
        );

        if ($xmlConfig->replace($this->getXpath(), $object) === false) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_SET_OBJECT_FAILED,
                $this->getXpath()
            );
        }

        $dispatcher = &OMVNotifyDispatcher::getInstance();
        $dispatcher->notify(
            OMV_NOTIFY_MODIFY,
            $this->getEventMessagePath(),
            $object
        );

        return $object;
    }

    public function doBackup($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams(
            $params,
            '{
                "type": "object",
                "properties": {
                    "sharedfolderref": { ' . $GLOBALS["OMV_JSONSCHEMA_UUID"] . ' }
                }
            }'
        );

        global $xmlConfig;

        // Sabnzbd needs to have run once to generate its
        // settings. Check if this is true
        if (!file_exists($this->sabnzbdSettingsFile)) {
            throw new OMVException(
                OMVErrorMsg::E_EXEC_MISC,
                "No settings folder found!"
            );
        }

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Create a background process.
        $bgStatusFilename = $this->createBgProcStatus();
        $pid = $this->fork();

        // Parent process.
        if ($pid > 0) {
            $this->initializeBgProcStatus($bgStatusFilename, $pid);
            return $bgStatusFilename;
        }

        // Stop service before backup.
        if (file_exists($this->sabnzbd2SettingsFile)) {
            $cmd = "/etc/init.d/sabnzbd2 stop";
            $this->exec($cmd);
        }

        // Get the Sickbeard module
        $moduleMgr = &OMVModuleMgr::getInstance();
        $sabnzbdModule = $moduleMgr->getModule($this->getName());
        // Child process.
        try {
            $bgOutputFilename = $this->createBgProcOutput();
            $this->updateBgProcStatus(
                $bgStatusFilename,
                "outputfilename",
                $bgOutputFilename
            );

            $backupPath = sprintf(
                "%s%ssabnzbd.tar.gz",
                $this->getSharedFolderPathByUuid($params["sharedfolderref"]),
                DIRECTORY_SEPARATOR
            );

            // Check for other instance.
            if (file_exists($this->sickbeard2SettingsFile)) {
                $cmd = sprintf(
                    "tar -zcvPf %s %s 2>&1",
                    $backupPath,
                    $this->sabnzbdSettingsDir . " " . $this->sabnzbd2SettingsDir
                );
            }else{
                $cmd = sprintf(
                    "tar -zcvPf %s %s 2>&1",
                    $backupPath,
                    $this->sabnzbdSettingsDir
                );
            }

            $this->exec($cmd, $output, $bgOutputFilename);
            $this->finalizeBgProcStatus($bgStatusFilename, $output);
            $sabnzbdModule->startService();
            exit(0);
        } catch (Exception $e) {
            $sabnzbdModule->startService();
            $this->finalizeBgProcStatus($bgStatusFilename, "", $e);
            exit(1);
        }
    }

    public function doRestore($params, $context)
    {
        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Validate the parameters of the RPC service method.
        $this->validateMethodParams(
            $params,
            '{
                "type": "object",
                "properties": {
                    "sharedfolderref": { ' . $GLOBALS["OMV_JSONSCHEMA_UUID"] . ' }
                }
            }'
        );

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        $backupPath = sprintf(
            "%s%sabnzbd.tar.gz",
            $this->getSharedFolderPathByUuid($params["sharedfolderref"]),
            DIRECTORY_SEPARATOR
        );

        if (is_null($backupPath)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED,
                $xPath
            );
        }

        // Check if backup is present.
        if (file_exists($backupPath)) {
            // Create a background process.
            $bgStatusFilename = $this->createBgProcStatus();
            $pid = $this->fork();

            // Parent process.
            if ($pid > 0) {
                $this->initializeBgProcStatus($bgStatusFilename, $pid);
                return $bgStatusFilename;
            }

            // Delete current settings folder
            rmdir($this->sabnzbdSettingsDir);

            // Delete current settings2 folder. Need to find a way of checking the archive to be sure settings2 is there
            if (file_exists($this->sabnzbd2SettingsFile)) {
                rmdir($this->sabnzbd2SettingsDir);
                $cmd = "/etc/init.d/sabnzbd2 stop";
                $this->exec($cmd);
            }

            // Get the Sabnzbd module
            $moduleMgr = &OMVModuleMgr::getInstance();
            $sabnzbdModule = $moduleMgr->getModule($this->getName());

            // Child process.
            try {
                $sabnzbdModule->stopService();
                $bgOutputFilename = $this->createBgProcOutput();
                $this->updateBgProcStatus(
                    $bgStatusFilename,
                    "outputfilename",
                    $bgOutputFilename
                );

                $cmd = sprintf("tar -zxvf %s -C / 2>&1", $backupPath);

                $this->exec($cmd, $output, $bgOutputFilename);
                $this->finalizeBgProcStatus($bgStatusFilename, $output);
                $sabnzbdModule->startService();
                exit(0);
            } catch (Exception $e) {
                $this->finalizeBgProcStatus($bgStatusFilename, "", $e);
                $sabnzbdModule->startService();
                exit(1);
            }
        } else {
            throw new OMVException(OMVErrorMsg::E_EXEC_MISC, "No backup found!");
        }
    }

    public function doUpdateSAB($params, $context)
    {
        $cmd = sprintf("omv-mkconf %s update 2>&1", $this->getPluginName());

        if (0 !== $this->exec($cmd, $output)) {
            throw new OMVException(
                OMVErrorMsg::E_EXEC_FAILED,
                $cmd,
                implode("\n", $output)
            );
        }

        $dispatcher = &OMVNotifyDispatcher::getInstance();
        $dispatcher->notify(
            OMV_NOTIFY_MODIFY,
            $this->getEventMessagePath(),
            $object
        );
    }
}

// Register the RPC service.
$rpcServiceMgr = &OMVRpcServiceMgr::getInstance();
$rpcServiceMgr->registerService(new OMVRpcServiceSabnzbd());
