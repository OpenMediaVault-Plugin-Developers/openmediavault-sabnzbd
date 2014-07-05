<?php

/**
 * Copyright (C) 2013-2014 OpenMediaVault Plugin Developers
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
    private $sabnzbdSettingsDir = "/home/sabnzbd/.sabnzbd/";
    private $sabnzbdSettingsFile = "/home/sabnzbd/.sabnzbd/sabnzbd.ini";

    private function getPluginName()
    {
        return strtolower($this->getName());
    }

    private function getEventMessagePath()
    {
        return sprintf("org.openmediavault.service.%s", $this->getPluginName());
    }

    private function getXPath()
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

        $object = $xmlConfig->get($this->getXPath());

        if (is_null($object)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED,
                $this->getXPath()
            );
        }

        $port = 8080;

        // Get current port as set in Sabnzbd config file
        if (file_exists($this->sabnzbdSettingsFile)) {
            $config = parse_ini_file(
                $this->sabnzbdSettingsFile,
                true,
                INI_SCANNER_RAW
            );

            if (!empty($config["misc"]["enable_https"])) {
                $sslIsEnabled = boolval($config["misc"]["enable_https"]);
            }
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

        //$output = array();
        //if (file_exists("/home/sabnzbd/.sabnzbd/sabnzbd.ini")) {
        //    $cmd = "grep -m1 ^enable_https /home/sabnzbd/.sabnzbd/sabnzbd.ini | cut -d ' ' -f 3 2>&1";
        //    $this->exec($cmd, $output);
        //    if ($output[0] == "1") {
        //        $cmd = "grep ^https_port /home/sabnzbd/.sabnzbd/sabnzbd.ini | cut -d ' ' -f 3 2>&1";
        //        $this->exec($cmd, $output);
        //        if ($output[0] == "")
        //            $output[0] = "9090";
        //    } else {
        //        $cmd = "grep -m 1 ^port /home/sabnzbd/.sabnzbd/sabnzbd.ini | cut -d ' ' -f 3";
        //        $this->exec($cmd, $output);
        //        if ($output[0] == "")
        //            $output[0] = "8080";
        //    }

        //}
        //if (! file_exists("/home/sabnzbd/.sabnzbd/sabnzbd.ini")) {
        //    $output[0] = "8080";
        //}

        $object["enable"]  = boolval($object["enable"]);
        $object["showtab"] = boolval($object["showtab"]);
        $object["update"]  = boolval($object["update"]);
        $object["port"]    = intval($port);

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
                    "enable"    : { "type" : "boolean" },
                    "showtab"   : { "type" : "boolean" },
                    "update"    : { "type" : "boolean" },
                    "port"      : { "type" : "integer" },
                    "mntentref" : { ' . $GLOBALS["OMV_JSONSCHEMA_UUID_NONE"] . ' },
                    "path"      : { "type" : "string", "optional" : true }
                }
            }'
        );


        $xPath = sprintf("//system/fstab/mntent[uuid='%s']", $params["mntentref"]);
        $deviceDir = $xmlConfig->get($xPath);
        $deviceDir = $deviceDir["dir"];


        $backupPath = "$deviceDir/SABbackup";
        $params["path"] = $backupPath;

        // Get existing configuration object
        $oldObject = $xmlConfig->get($this->getXPath());

        if (is_null($oldObject)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_GET_OBJECT_FAILED,
                $this->getXPath()
            );
        }

        $object = array(
            "enable"    => array_boolval($params, "enable"),
            "showtab"   => array_boolval($params, "showtab"),
            "update"    => array_boolval($params, "update"),
            "port"      => $params["port"],
            "mntentref" => $params["mntentref"],
            "path"      => $params["path"],
        );

        if (false === $xmlConfig->replace($this->getXPath(), $object)) {
            throw new OMVException(
                OMVErrorMsg::E_CONFIG_SET_OBJECT_FAILED,
                $this->getXPath()
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

        // Child process.
        try {
            $bgOutputFilename = $this->createBgProcOutput();
            $this->updateBgProcStatus(
                $bgStatusFilename,
                "outputfilename",
                $bgOutputFilename
            );

            // Get the configuration object.
            $xPath = sprintf("%s/path", $this->getXPath());
            $backupPath = $xmlConfig->get($xPath);

            $cmd = sprintf(
                "tar -zcvPf %s %s 2>&1",
                $backupPath,
                $this->sabnzbdSettingsDir
            );

            $this->exec($cmd, $output, $bgOutputFilename);
            $this->finalizeBgProcStatus($bgStatusFilename, $output);
            exit(0);
        } catch (Exception $e) {
            $this->finalizeBgProcStatus($bgStatusFilename, "", $e);
            exit(1);
        }
    }

    public function doRestore($params, $context)
    {
        global $xmlConfig;

        // Validate the RPC caller context.
        $this->validateMethodContext($context, array(
            "role" => OMV_ROLE_ADMINISTRATOR
        ));

        // Get the configuration object.
        $xPath = sprintf("%s/path", $this->getXPath());
        $backupPath = $xmlConfig->get($xPath);

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
