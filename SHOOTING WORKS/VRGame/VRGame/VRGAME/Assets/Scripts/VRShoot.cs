using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class VRShoot : MonoBehaviour {

    // Use this for initialization
    public GameObject BulletPrefab;
    public Transform launchPosition;
    public int bulletSpeed;

    public bool isLoaded;

	void Start () {

        isLoaded = true;
		
	}
	
	// Update is called once per frame
	void Update () {
		if(OVRInput.GetDown(OVRInput.Button.SecondaryIndexTrigger))
        {
            if(isLoaded == true)
            {
                shootBullet(); // (Bullet);
            }
        }
	}

    void shootBullet()  //GameObject Bullet)
    {
        Debug.Log("Pulled the trigger");
        GameObject shotBullet = Instantiate(BulletPrefab) as GameObject;
        shotBullet.transform.position = launchPosition.position;
        shotBullet.transform.rotation = launchPosition.rotation;

        shotBullet.GetComponent<Rigidbody>().AddForce(launchPosition.forward * bulletSpeed, ForceMode.Impulse);


    }
}
