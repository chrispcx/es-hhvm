<?hh
/**
 * Autogenerated by Thrift
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */

enum EmptyEnum: int {
}
type EmptyEnumType = EmptyEnum;

enum City: int {
  NYC = 0;
  MPK = 1;
  SEA = 2;
  LON = 3;
}
type CityType = City;

enum Company: int {
  FACEBOOK = 0;
  WHATSAPP = 1;
  OCULUS = 2;
  INSTAGRAM = 3;
}
type CompanyType = Company;

class Internship implements \IThriftStruct {
  public static array $_TSPEC = array(
    1 => array(
      'var' => 'weeks',
      'type' => \TType::I32,
      ),
    2 => array(
      'var' => 'title',
      'type' => \TType::STRING,
      ),
    3 => array(
      'var' => 'employer',
      'type' => \TType::I32,
      'enum' => 'Company',
      ),
    );
  public static Map<string, int> $_TFIELDMAP = Map {
    'weeks' => 1,
    'title' => 2,
    'employer' => 3,
  };
  const int STRUCTURAL_ID = 749038867953722654;
  public int $weeks;
  public string $title;
  public ?Company $employer;

  public function __construct(?int $weeks = null, ?string $title = null, ?Company $employer = null  ) {
    if ($weeks === null) {
      $this->weeks = 0;
    } else {
      $this->weeks = $weeks;
    }
    if ($title === null) {
      $this->title = '';
    } else {
      $this->title = $title;
    }
    $this->employer = $employer;
  }

  public function getName(): string {
    return 'Internship';
  }

  public function read(\TProtocol $input): int {
    $xfer = 0;
    $fname = '';
    $ftype = 0;
    $fid = 0;
    $weeks__isset = false;
    $xfer += $input->readStructBegin($fname);
    while (true)
    {
      $xfer += $input->readFieldBegin($fname, $ftype, $fid);
      if ($ftype == \TType::STOP) {
        break;
      }
      if (!$fid && $fname !== null) {
        $fid = (int) self::$_TFIELDMAP->get($fname);
        if ($fid !== 0) {
          $ftype = self::$_TSPEC[$fid]['type'];
        }
      }
      switch ($fid)
      {
        case 1:
          if ($ftype == \TType::I32) {
            $xfer += $input->readI32($this->weeks);
            $weeks__isset = true;
          } else {
            $xfer += $input->skip($ftype);
          }
          break;
        case 2:
          if ($ftype == \TType::STRING) {
            $xfer += $input->readString($this->title);
          } else {
            $xfer += $input->skip($ftype);
          }
          break;
        case 3:
          if ($ftype == \TType::I32) {
            $_val0 = null;
            $xfer += $input->readI32($_val0);
            $this->employer = Company::coerce($_val0);

          } else {
            $xfer += $input->skip($ftype);
          }
          break;
        default:
          $xfer += $input->skip($ftype);
          break;
      }
      $xfer += $input->readFieldEnd();
    }
    $xfer += $input->readStructEnd();
    if (!$weeks__isset) {
      throw new \TProtocolException("Required field 'weeks' was not found in serialized data! Struct: Internship", \TProtocolException::MISSING_REQUIRED_FIELD);
    }
    return $xfer;
  }

  public function write(\TProtocol $output): int {
    $xfer = 0;
    $xfer += $output->writeStructBegin('Internship');
    if ($this->weeks !== null) {
      $_val0 = $this->weeks;
      $xfer += $output->writeFieldBegin('weeks', \TType::I32, 1);
      $xfer += $output->writeI32($_val0);
      $xfer += $output->writeFieldEnd();
    }
    if ($this->title !== null) {
      $_val1 = $this->title;
      $xfer += $output->writeFieldBegin('title', \TType::STRING, 2);
      $xfer += $output->writeString($_val1);
      $xfer += $output->writeFieldEnd();
    }
    if ($this->employer !== null) {
      $_val2 = Company::assert($this->employer);
      $xfer += $output->writeFieldBegin('employer', \TType::I32, 3);
      $xfer += $output->writeI32($_val2);
      $xfer += $output->writeFieldEnd();
    }
    $xfer += $output->writeFieldStop();
    $xfer += $output->writeStructEnd();
    return $xfer;
  }

}

class UnEnumStruct implements \IThriftStruct {
  public static array $_TSPEC = array(
    1 => array(
      'var' => 'city',
      'type' => \TType::I32,
      'enum' => 'City',
      ),
    );
  public static Map<string, int> $_TFIELDMAP = Map {
    'city' => 1,
  };
  const int STRUCTURAL_ID = 8709689501091584749;
  public ?City $city;

  public function __construct(?City $city = null  ) {
    if ($city === null) {
      $this->city = City::coerce(-1);
    } else {
      $this->city = $city;
    }
  }

  public function getName(): string {
    return 'UnEnumStruct';
  }

  public function read(\TProtocol $input): int {
    $xfer = 0;
    $fname = '';
    $ftype = 0;
    $fid = 0;
    $xfer += $input->readStructBegin($fname);
    while (true)
    {
      $xfer += $input->readFieldBegin($fname, $ftype, $fid);
      if ($ftype == \TType::STOP) {
        break;
      }
      if (!$fid && $fname !== null) {
        $fid = (int) self::$_TFIELDMAP->get($fname);
        if ($fid !== 0) {
          $ftype = self::$_TSPEC[$fid]['type'];
        }
      }
      switch ($fid)
      {
        case 1:
          if ($ftype == \TType::I32) {
            $_val0 = null;
            $xfer += $input->readI32($_val0);
            $this->city = City::coerce($_val0);

          } else {
            $xfer += $input->skip($ftype);
          }
          break;
        default:
          $xfer += $input->skip($ftype);
          break;
      }
      $xfer += $input->readFieldEnd();
    }
    $xfer += $input->readStructEnd();
    return $xfer;
  }

  public function write(\TProtocol $output): int {
    $xfer = 0;
    $xfer += $output->writeStructBegin('UnEnumStruct');
    if ($this->city !== null) {
      $_val0 = City::assert($this->city);
      $xfer += $output->writeFieldBegin('city', \TType::I32, 1);
      $xfer += $output->writeI32($_val0);
      $xfer += $output->writeFieldEnd();
    }
    $xfer += $output->writeFieldStop();
    $xfer += $output->writeStructEnd();
    return $xfer;
  }

}

class Range implements \IThriftStruct {
  public static array $_TSPEC = array(
    1 => array(
      'var' => 'min',
      'type' => \TType::I32,
      ),
    2 => array(
      'var' => 'max',
      'type' => \TType::I32,
      ),
    );
  public static Map<string, int> $_TFIELDMAP = Map {
    'min' => 1,
    'max' => 2,
  };
  const int STRUCTURAL_ID = 6850388386457434767;
  public int $min;
  public int $max;

  public function __construct(?int $min = null, ?int $max = null  ) {
    if ($min === null) {
      $this->min = 0;
    } else {
      $this->min = $min;
    }
    if ($max === null) {
      $this->max = 0;
    } else {
      $this->max = $max;
    }
  }

  public function getName(): string {
    return 'Range';
  }

  public function read(\TProtocol $input): int {
    $xfer = 0;
    $fname = '';
    $ftype = 0;
    $fid = 0;
    $min__isset = false;
    $max__isset = false;
    $xfer += $input->readStructBegin($fname);
    while (true)
    {
      $xfer += $input->readFieldBegin($fname, $ftype, $fid);
      if ($ftype == \TType::STOP) {
        break;
      }
      if (!$fid && $fname !== null) {
        $fid = (int) self::$_TFIELDMAP->get($fname);
        if ($fid !== 0) {
          $ftype = self::$_TSPEC[$fid]['type'];
        }
      }
      switch ($fid)
      {
        case 1:
          if ($ftype == \TType::I32) {
            $xfer += $input->readI32($this->min);
            $min__isset = true;
          } else {
            $xfer += $input->skip($ftype);
          }
          break;
        case 2:
          if ($ftype == \TType::I32) {
            $xfer += $input->readI32($this->max);
            $max__isset = true;
          } else {
            $xfer += $input->skip($ftype);
          }
          break;
        default:
          $xfer += $input->skip($ftype);
          break;
      }
      $xfer += $input->readFieldEnd();
    }
    $xfer += $input->readStructEnd();
    if (!$min__isset) {
      throw new \TProtocolException("Required field 'min' was not found in serialized data! Struct: Range", \TProtocolException::MISSING_REQUIRED_FIELD);
    }
    if (!$max__isset) {
      throw new \TProtocolException("Required field 'max' was not found in serialized data! Struct: Range", \TProtocolException::MISSING_REQUIRED_FIELD);
    }
    return $xfer;
  }

  public function write(\TProtocol $output): int {
    $xfer = 0;
    $xfer += $output->writeStructBegin('Range');
    if ($this->min !== null) {
      $_val0 = $this->min;
      $xfer += $output->writeFieldBegin('min', \TType::I32, 1);
      $xfer += $output->writeI32($_val0);
      $xfer += $output->writeFieldEnd();
    }
    if ($this->max !== null) {
      $_val1 = $this->max;
      $xfer += $output->writeFieldBegin('max', \TType::I32, 2);
      $xfer += $output->writeI32($_val1);
      $xfer += $output->writeFieldEnd();
    }
    $xfer += $output->writeFieldStop();
    $xfer += $output->writeStructEnd();
    return $xfer;
  }

}
